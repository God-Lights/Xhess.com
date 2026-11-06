// 릴리스 빌드에서 Windows 콘솔 창이 뜨지 않게 한다.
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod engine;

use tauri::{Manager, RunEvent};

fn main() {
    tauri::Builder::default()
        .manage(engine::EngineState::default())
        .invoke_handler(tauri::generate_handler![
            engine::engine_start,
            engine::engine_send,
            engine::engine_stop
        ])
        .build(tauri::generate_context!())
        .expect("error while building Xhess")
        .run(|app, event| {
            // 앱이 끝날 때 엔진 프로세스가 남지 않게 한다.
            if let RunEvent::Exit = event {
                app.state::<engine::EngineState>().shutdown();
            }
        });
}
