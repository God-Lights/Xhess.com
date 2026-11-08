import { describe, expect, it } from "vitest";
import { ThemeClient } from "./themeClient";
import type { Transport } from "./engine/uci";

/** 가짜 Albatross: uci → uciok, position 기억, themes → 준비된 줄들. */
function fake(opts: { respond?: boolean; delay?: number } = {}) {
  const respond = opts.respond ?? true;
  const received: string[] = [];
  let cb: (l: string) => void = () => {};
  let fen = "";
  const t: Transport = {
    send(cmd) {
      received.push(cmd);
      if (!respond) return;
      if (cmd === "uci") setTimeout(() => cb("uciok"), 1);
      else if (cmd.startsWith("position fen ")) fen = cmd.slice("position fen ".length);
      else if (cmd === "themes") {
        setTimeout(() => {
          cb(`theme passed white e5 cp ${fen.length} mg 1 eg 1`);
          cb("themes done phase 24 scale 128 white 5");
        }, opts.delay ?? 1);
      }
    },
    onLine: (f) => (cb = f),
    terminate() { received.push("<terminated>"); },
  };
  return { t, received };
}

describe("ThemeClient", () => {
  it("엔진을 한 번만 시작하고 요청마다 position + themes 를 보낸다", async () => {
    const f = fake();
    let made = 0;
    const c = new ThemeClient(() => { made++; return f.t; });
    const a = await c.request("8/8/8/4P3/8/8/8/k6K w - - 0 1");
    const b = await c.request("8/8/8/4P3/8/8/8/k5K1 w - - 0 1");
    expect(made).toBe(1);
    expect(f.received.filter((x) => x === "uci")).toHaveLength(1);
    expect(f.received.filter((x) => x === "themes")).toHaveLength(2);
    expect(a?.themes[0].cp).toBe("8/8/8/4P3/8/8/8/k6K w - - 0 1".length);
    expect(b?.themes[0].cp).toBe("8/8/8/4P3/8/8/8/k5K1 w - - 0 1".length);
    expect(a?.white).toBe(5);
  });

  it("요청이 겹치면 마지막 요청만 계산하고 앞선 요청은 null", async () => {
    const f = fake({ delay: 5 });
    const c = new ThemeClient(() => f.t);
    const first = c.request("1");
    const second = c.request("22");
    const third = c.request("333");
    const [r1, r2, r3] = await Promise.all([first, second, third]);
    expect(r1).toBeNull();
    expect(r2).toBeNull();
    expect(r3?.themes[0].cp).toBe(3);
    expect(f.received.filter((x) => x === "themes")).toHaveLength(1);
  });

  it("엔진이 응답하지 않으면 오류를 던지고, 다음 요청에서 다시 시도한다", async () => {
    const silent = fake({ respond: false });
    const good = fake();
    let n = 0;
    const c = new ThemeClient(() => (n++ === 0 ? silent.t : good.t), { timeoutMs: 30 });
    await expect(c.request("x")).rejects.toThrow("응답");
    expect(silent.received).toContain("<terminated>");
    const r = await c.request("abc");
    expect(r?.themes[0].cp).toBe(3);
    expect(n).toBe(2);
  });

  it("themes 를 모르는 옛 엔진(종료 줄이 안 옴)은 시간 초과", async () => {
    const f = fake();
    const t = f.t;
    // uci 에는 답하지만 themes 에는 답하지 않는 엔진
    let cb: (l: string) => void = () => {};
    const old: Transport = {
      send(cmd) { if (cmd === "uci") setTimeout(() => cb("uciok"), 1); else if (cmd === "themes") setTimeout(() => cb("info string unknown command: themes"), 1); },
      onLine: (f2) => (cb = f2),
      terminate() {},
    };
    void t;
    const c = new ThemeClient(() => old, { timeoutMs: 40 });
    await expect(c.request("x")).rejects.toThrow("응답");
  });

  it("엔진이 시작되기 전에 dispose 하면 엔진을 아예 시작하지 않고 null", async () => {
    const f = fake({ delay: 20 });
    const c = new ThemeClient(() => f.t);
    const p = c.request("a");
    c.dispose();
    expect(await p).toBeNull();
    expect(f.received).toEqual([]);   // uci 조차 보내지 않았다
  });

  it("실행 중에 dispose 하면 엔진을 종료하고, 이후 요청은 새로 시작한다", async () => {
    const f1 = fake({ delay: 5 });
    const f2 = fake();
    let n = 0;
    const c = new ThemeClient(() => (n++ === 0 ? f1.t : f2.t));
    await c.request("first");
    c.dispose();
    expect(f1.received).toContain("<terminated>");
    const again = await c.request("second");
    expect(again?.themes[0].cp).toBe("second".length);
    expect(n).toBe(2);
  });
});
