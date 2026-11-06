import type { Transport } from "./uci";

// withGlobalTauri 가 켜져 있으면 데스크톱 앱의 웹뷰에 window.__TAURI__ 가 주입된다.
interface TauriGlobal {
  core: { invoke<T = unknown>(cmd: string, args?: Record<string, unknown>): Promise<T> };
  event: { listen<T>(name: string, cb: (e: { payload: T }) => void): Promise<() => void> };
}

function tauri(): TauriGlobal | undefined {
  return (globalThis as unknown as { __TAURI__?: TauriGlobal }).__TAURI__;
}

export function isTauri(): boolean {
  return tauri() !== undefined;
}

export interface NativeTransport extends Transport {
  /** 실제로 실행된 엔진 경로 */
  path: string;
  /** 엔진 프로세스가 끝나면 호출된다 */
  onExit(cb: () => void): void;
}

/**
 * 데스크톱: Rust 가 띄운 네이티브 UCI 엔진과 연결한다.
 * @param customPath 비우면 번들된 Stockfish, 아니면 그 경로의 UCI 실행 파일
 */
export async function createTauriTransport(customPath?: string): Promise<NativeTransport> {
  const t = tauri();
  if (!t) throw new Error("Tauri 환경이 아닙니다");

  let lineCb: (line: string) => void = () => {};
  let exitCb: () => void = () => {};
  // 시작 전에 리스너부터 달아 첫 출력(id/uciok)을 놓치지 않는다.
  const unlistenLine = await t.event.listen<string>("engine-line", (e) => lineCb(e.payload.trim()));
  const unlistenExit = await t.event.listen<null>("engine-exit", () => exitCb());

  let path: string;
  try {
    path = await t.core.invoke<string>("engine_start", { path: customPath?.trim() || null });
  } catch (e) {
    unlistenLine();
    unlistenExit();
    throw e instanceof Error ? e : new Error(String(e));
  }

  // IPC 호출은 비동기이므로 명령 순서가 뒤바뀌지 않게 직렬화한다.
  let queue: Promise<unknown> = Promise.resolve();
  return {
    path,
    send(cmd) {
      queue = queue.then(() => t.core.invoke("engine_send", { line: cmd })).catch(() => {});
    },
    onLine: (cb) => (lineCb = cb),
    onExit: (cb) => (exitCb = cb),
    terminate() {
      unlistenLine();
      unlistenExit();
      queue = queue.then(() => t.core.invoke("engine_stop")).catch(() => {});
    },
  };
}
