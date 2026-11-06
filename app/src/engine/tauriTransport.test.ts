import { afterEach, describe, expect, it } from "vitest";
import { createTauriTransport, isTauri } from "./tauriTransport";

type Handler = (e: { payload: unknown }) => void;

function installFakeTauri(opts: { failStart?: boolean } = {}) {
  const handlers = new Map<string, Handler>();
  const calls: Array<{ cmd: string; args?: Record<string, unknown> }> = [];
  (globalThis as Record<string, unknown>).__TAURI__ = {
    core: {
      invoke: async (cmd: string, args?: Record<string, unknown>) => {
        calls.push({ cmd, args });
        if (cmd === "engine_start") {
          if (opts.failStart) throw "엔진 실행 실패";
          return "/opt/stockfish";
        }
      },
    },
    event: {
      listen: async (name: string, cb: Handler) => {
        handlers.set(name, cb);
        return () => handlers.delete(name);
      },
    },
  };
  return { handlers, calls };
}

afterEach(() => delete (globalThis as Record<string, unknown>).__TAURI__);

describe("tauri transport", () => {
  it("브라우저에서는 Tauri 로 인식하지 않는다", () => {
    expect(isTauri()).toBe(false);
  });

  it("줄 전달과 명령 순서", async () => {
    const { handlers, calls } = installFakeTauri();
    expect(isTauri()).toBe(true);
    const t = await createTauriTransport("  ");
    expect(t.path).toBe("/opt/stockfish");
    expect(calls[0]).toEqual({ cmd: "engine_start", args: { path: null } }); // 공백 경로 = 번들 엔진

    const lines: string[] = [];
    t.onLine((l) => lines.push(l));
    handlers.get("engine-line")!({ payload: "uciok\r" });
    expect(lines).toEqual(["uciok"]);

    t.send("uci");
    t.send("isready");
    t.send("go movetime 100");
    await new Promise((r) => setTimeout(r, 10));
    expect(calls.filter((c) => c.cmd === "engine_send").map((c) => c.args!.line)).toEqual([
      "uci",
      "isready",
      "go movetime 100",
    ]);
  });

  it("사용자 지정 경로를 넘기고 종료 시 정리한다", async () => {
    const { handlers, calls } = installFakeTauri();
    const t = await createTauriTransport(" /usr/games/albatross ");
    expect(calls[0].args).toEqual({ path: "/usr/games/albatross" });
    let exited = false;
    t.onExit(() => (exited = true));
    handlers.get("engine-exit")!({ payload: null });
    expect(exited).toBe(true);
    t.terminate();
    await new Promise((r) => setTimeout(r, 10));
    expect(calls.at(-1)!.cmd).toBe("engine_stop");
    expect(handlers.size).toBe(0);
  });

  it("시작 실패 시 오류를 던지고 리스너를 정리한다", async () => {
    const { handlers } = installFakeTauri({ failStart: true });
    await expect(createTauriTransport()).rejects.toThrow("엔진 실행 실패");
    expect(handlers.size).toBe(0);
  });
});
