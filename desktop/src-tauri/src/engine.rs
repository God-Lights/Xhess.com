//! 로컬 UCI 엔진 프로세스 관리.
//!
//! 프런트엔드는 `engine_start` / `engine_send` / `engine_stop` 명령과 `engine-line` 이벤트만 쓴다.
//! 번들된 Stockfish(sidecar) 대신 임의의 UCI 실행 파일 경로도 줄 수 있어, 나중에 Albatross 를 그대로 붙일 수 있다.

use std::io::{BufRead, BufReader, Write};
use std::path::PathBuf;
use std::process::{Child, ChildStdin, Command, Stdio};
use std::sync::Mutex;
use std::thread;

use tauri::{AppHandle, Emitter, State};

/// 프로세스와 stdin. 한 번에 하나의 엔진만 돌린다.
struct Running {
    child: Child,
    stdin: ChildStdin,
}

#[derive(Default)]
pub struct EngineState(Mutex<Option<Running>>);

impl EngineState {
    /// 앱 종료 시 자식 프로세스를 정리한다.
    pub fn shutdown(&self) {
        if let Some(mut r) = self.0.lock().unwrap().take() {
            stop(&mut r);
        }
    }
}

fn stop(r: &mut Running) {
    let _ = writeln!(r.stdin, "quit");
    let _ = r.stdin.flush();
    // 정상 종료를 잠깐 기다린 뒤에도 살아 있으면 강제 종료한다.
    for _ in 0..20 {
        if matches!(r.child.try_wait(), Ok(Some(_))) {
            return;
        }
        thread::sleep(std::time::Duration::from_millis(25));
    }
    let _ = r.child.kill();
    let _ = r.child.wait();
}

/// 번들 엔진: 설치 시 메인 실행 파일 옆에 `xhess-stockfish[.exe]` 로 놓인다(Tauri externalBin).
fn bundled_path() -> Result<PathBuf, String> {
    let exe = std::env::current_exe().map_err(|e| e.to_string())?;
    let dir = exe.parent().ok_or("실행 파일 위치를 알 수 없습니다")?;
    Ok(dir.join(format!("xhess-stockfish{}", std::env::consts::EXE_SUFFIX)))
}

fn spawn(path: &PathBuf) -> Result<Child, String> {
    let mut cmd = Command::new(path);
    cmd.stdin(Stdio::piped()).stdout(Stdio::piped()).stderr(Stdio::null());
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        cmd.creation_flags(0x0800_0000); // CREATE_NO_WINDOW: 콘솔 창이 뜨지 않게
    }
    cmd.spawn().map_err(|e| format!("엔진 실행 실패 ({}): {e}", path.display()))
}

/// 엔진을 (재)시작한다. `path` 가 없으면 번들 Stockfish 를 쓴다. 실행한 경로를 돌려준다.
#[tauri::command]
pub fn engine_start(
    app: AppHandle,
    state: State<'_, EngineState>,
    path: Option<String>,
) -> Result<String, String> {
    let mut slot = state.0.lock().unwrap();
    if let Some(mut old) = slot.take() {
        stop(&mut old);
    }

    let path = match path.as_deref().map(str::trim).filter(|p| !p.is_empty()) {
        Some(p) => PathBuf::from(p),
        None => bundled_path()?,
    };
    let mut child = spawn(&path)?;
    let stdin = child.stdin.take().ok_or("stdin 을 열 수 없습니다")?;
    let stdout = child.stdout.take().ok_or("stdout 을 열 수 없습니다")?;

    // 엔진 출력을 줄 단위로 프런트엔드에 전달한다. 프로세스가 끝나면 EOF 로 스레드도 끝난다.
    thread::spawn(move || {
        for line in BufReader::new(stdout).lines().map_while(Result::ok) {
            let _ = app.emit("engine-line", line);
        }
        let _ = app.emit("engine-exit", ());
    });

    *slot = Some(Running { child, stdin });
    Ok(path.display().to_string())
}

#[tauri::command]
pub fn engine_send(state: State<'_, EngineState>, line: String) -> Result<(), String> {
    let mut slot = state.0.lock().unwrap();
    let r = slot.as_mut().ok_or("엔진이 실행 중이 아닙니다")?;
    writeln!(r.stdin, "{line}").map_err(|e| e.to_string())?;
    r.stdin.flush().map_err(|e| e.to_string())
}

#[tauri::command]
pub fn engine_stop(state: State<'_, EngineState>) {
    state.shutdown();
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn bundled_path_is_next_to_the_executable() {
        let p = bundled_path().unwrap();
        assert!(p.file_name().unwrap().to_string_lossy().starts_with("xhess-stockfish"));
        assert_eq!(p.parent(), std::env::current_exe().unwrap().parent());
    }

    #[test]
    fn spawn_reports_a_missing_engine() {
        let err = spawn(&PathBuf::from("/definitely/not/an/engine")).unwrap_err();
        assert!(err.contains("엔진 실행 실패"));
    }

    #[cfg(unix)]
    #[test]
    fn lines_round_trip_through_a_child_process() {
        // `cat` 이 stdin 을 stdout 으로 그대로 돌려주므로 UCI 파이프와 같은 경로를 검증할 수 있다.
        let mut r = {
            let mut child = spawn(&PathBuf::from("cat")).unwrap();
            let stdin = child.stdin.take().unwrap();
            let stdout = child.stdout.take().unwrap();
            let (tx, rx) = std::sync::mpsc::channel();
            thread::spawn(move || {
                for l in BufReader::new(stdout).lines().map_while(Result::ok) {
                    let _ = tx.send(l);
                }
            });
            (Running { child, stdin }, rx)
        };
        writeln!(r.0.stdin, "uci").unwrap();
        writeln!(r.0.stdin, "isready").unwrap();
        r.0.stdin.flush().unwrap();
        let t = std::time::Duration::from_secs(5);
        assert_eq!(r.1.recv_timeout(t).unwrap(), "uci");
        assert_eq!(r.1.recv_timeout(t).unwrap(), "isready");
        stop(&mut r.0);
        assert!(r.0.child.try_wait().unwrap().is_some());
    }
}
