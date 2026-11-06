import { describe, expect, it } from "vitest";
import type { SearchInfo } from "./engine/uci";
import {
  DEFAULT_PARAMS, SOUND_LOSS, chooseMove, classify, enumerateSacrifices, kindOf, makePool, mulberry32,
  sacrificeInfo, type PoolEntry,
} from "./humanmode";

const START = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
const line = (uci: string, cp: number, extra: Partial<SearchInfo> = {}): SearchInfo => ({ multipv: 1, depth: 12, cp, pv: [uci], ...extra });

describe("sacrificeInfo (포획 교환이 끝난 뒤의 물질 변화)", () => {
  it("조용한 수와 이득 보는 포획은 희생이 아니다", () => {
    expect(sacrificeInfo(START, "e2e4")).toBeNull();
    expect(sacrificeInfo("4k3/8/8/3p4/4P3/8/8/4K3 w - - 0 1", "e4d5")).toBeNull();        // 공짜 폰
    expect(sacrificeInfo("4k3/8/2p5/3p4/4P3/8/8/4K3 w - - 0 1", "e4d5")).toBeNull();      // 폰 맞교환
  });

  it("퀸이 폰 하나를 잡고 폰에게 잡히면 큰 희생이다", () => {
    const s = sacrificeInfo("4k3/8/2p5/3p4/8/8/3Q4/4K3 w - - 0 1", "d2d5");
    expect(s).toEqual({ swing: -800, kind: "major" });
  });

  it("기물을 공격받는 칸에 두면 그 기물 값만큼의 희생", () => {
    const s = sacrificeInfo("4k3/8/8/3p4/8/2N5/8/4K3 w - - 0 1", "c3e4"); // Ne4?, dxe4
    expect(s).toEqual({ swing: -320, kind: "piece" });
  });

  it("교환 희생: 룩으로 폰이 지키는 나이트를 잡는다", () => {
    const s = sacrificeInfo("4k3/8/2p5/3n4/8/8/3R4/4K3 w - - 0 1", "d2d5"); // Rxd5 cxd5: +320 -500
    expect(s).toEqual({ swing: -180, kind: "exchange" });
  });

  it("폰 갬빗(폰 하나를 그냥 내줌)과 불법 수", () => {
    const s = sacrificeInfo("4k3/8/8/3p4/4P3/8/8/4K3 w - - 0 1", "e4e5"); // e5 는 안전, 희생 아님
    expect(s).toBeNull();
    const g = sacrificeInfo("4k3/8/2p5/8/3P4/8/8/4K3 w - - 0 1", "d4d5"); // d5 cxd5: 폰을 내줌
    expect(g).toEqual({ swing: -100, kind: "pawn" });
    expect(sacrificeInfo(START, "e2e5")).toBeNull();
  });

  it("종류 구간", () => {
    expect(kindOf(-100)).toBe("pawn");
    expect(kindOf(-180)).toBe("exchange");
    expect(kindOf(-320)).toBe("piece");
    expect(kindOf(-900)).toBe("major");
  });
});

describe("enumerateSacrifices", () => {
  it("시작 국면에는 희생이 없다", () => {
    expect(enumerateSacrifices(START)).toEqual([]);
  });

  it("전술 국면에서 희생 수를 찾는다", () => {
    // 2.Nf3 Nc6 뒤의 백: Nxe5 는 폰을 잡지만 Nxe5 로 나이트를 잃는다(폰 100 - 나이트 320 = -220).
    const sacs = enumerateSacrifices("r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 0 1");
    const nxe5 = sacs.find((s) => s.uci === "f3e5");
    expect(nxe5).toBeDefined();
    expect(nxe5!.info).toEqual({ swing: -220, kind: "exchange" });
    expect(nxe5!.capture).toBe(true);
    expect(sacs.map((s) => s.uci)).not.toContain("d2d3"); // 평범한 수는 희생이 아니다
  });

  it("킹이 잡을 수 없는 방어된 폰을 비숍이 잡는 것은 희생이 아니다 (합법성 반영)", () => {
    // 스칼라 포지션: Bxf7+ 는 퀸 h5 가 f7 을 받쳐서 Kxf7 이 불법이므로 안전한 수.
    const sacs = enumerateSacrifices("r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 4 4");
    const ucis = sacs.map((s) => s.uci);
    expect(ucis).not.toContain("c4f7");
    expect(ucis).not.toContain("h5f7"); // Qxf7# 도 마찬가지
  });

  it("체크를 주는 수는 상대가 다른 곳을 잡지 못한다 (체크 응수만 고려)", () => {
    // 스칼라 포지션에서 백 퀸 h5 는 흑 나이트 f6 에게 공격받고 있지만, Bxf7+ 는 체크라 상대가 퀸을 잡을 수 없다.
    const fen = "r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 4 4";
    expect(sacrificeInfo(fen, "c4f7")).toBeNull();
    // 반면 체크가 아닌 수(예: d3)는 퀸이 잡히므로 대형 희생이다.
    expect(sacrificeInfo(fen, "d2d3")).toEqual({ swing: -900, kind: "major" });
  });

  it("복잡한 국면에서도 빠르게 끝난다", () => {
    const t0 = performance.now();
    const sacs = enumerateSacrifices("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    expect(performance.now() - t0).toBeLessThan(1500);
    expect(Array.isArray(sacs)).toBe(true);
  });
});

describe("makePool", () => {
  it("MultiPV 후보와 따로 평가한 희생 수를 합치고 최선 대비 손해를 계산한다", () => {
    const fen = "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 0 1";
    const sacs = enumerateSacrifices(fen);
    const pool = makePool(fen, [line("d2d3", 30), line("b1c3", -10)], [{ uci: "f3e5", info: line("f3e5", -250) }], sacs);
    expect(pool.map((p) => [p.san, p.loss, p.speculative])).toEqual([["d3", 0, false], ["Nc3", 40, false], ["Nxe5", 280, true]]);
    expect(pool[2].sacrifice).not.toBeNull();
    expect(pool[0].sacrifice).toBeNull();
  });

  it("중복·불법 수는 무시한다", () => {
    const pool = makePool(START, [line("e2e4", 20), line("e2e4", 20), line("e2e5", 99)], [], []);
    expect(pool.map((p) => p.san)).toEqual(["e4"]);
    expect(makePool(START, [], [], [])).toEqual([]);
  });
});

describe("chooseMove", () => {
  const entry = (uci: string, loss: number, sac: PoolEntry["sacrifice"] = null, speculative = false): PoolEntry =>
    ({ uci, san: uci, score: 100 - loss, loss, sacrifice: sac, speculative });
  const piece = { swing: -320, kind: "piece" as const };

  const frequency = (pool: PoolEntry[], params: { humanness: number; boldness: number }, n = 4000) => {
    const rng = mulberry32(12345);
    const counts: Record<string, number> = {};
    for (let i = 0; i < n; i++) {
      const c = chooseMove(pool, params, rng)!;
      counts[c.uci] = (counts[c.uci] ?? 0) + 1;
    }
    return (u: string) => (counts[u] ?? 0) / n;
  };

  it("빈 후보는 null", () => {
    expect(chooseMove([], DEFAULT_PARAMS, mulberry32(1))).toBeNull();
  });

  it("humanness 0, boldness 0 이면 거의 항상 최선", () => {
    const pool = [entry("best", 0), entry("ok", 30), entry("bad", 120), entry("sac", 400, piece, true)];
    const f = frequency(pool, { humanness: 0, boldness: 0 });
    expect(f("best")).toBeGreaterThan(0.8);
    expect(f("sac")).toBe(0); // boldness 0 이면 손해 큰 희생은 절대 두지 않는다
  });

  it("humanness 가 클수록 최선이 아닌 수를 더 자주 둔다", () => {
    const pool = [entry("best", 0), entry("ok", 30), entry("bad", 120)];
    const low = frequency(pool, { humanness: 0.1, boldness: 0 })("best");
    const high = frequency(pool, { humanness: 0.9, boldness: 0 })("best");
    expect(low).toBeGreaterThan(high + 0.15);
  });

  it("boldness 가 클수록 손해 큰 희생을 자주 시도하고, 덜 나쁜 희생을 더 선호한다", () => {
    const pool = [entry("best", 0), entry("sacSoft", 120, piece, true), entry("sacBad", 600, piece, true)];
    const none = frequency(pool, { humanness: 0.4, boldness: 0 });
    const some = frequency(pool, { humanness: 0.4, boldness: 0.5 });
    const full = frequency(pool, { humanness: 0.4, boldness: 1 });
    expect(none("sacSoft") + none("sacBad")).toBe(0);
    const total = (f: (u: string) => number) => f("sacSoft") + f("sacBad");
    expect(total(some)).toBeGreaterThan(0.08);
    expect(total(full)).toBeGreaterThan(total(some));
    expect(total(full)).toBeLessThan(0.5);       // 늘 희생만 하지는 않는다
    expect(full("sacSoft")).toBeGreaterThan(full("sacBad")); // 그래도 덜 나쁜 쪽 선호
    expect(full("sacBad")).toBeGreaterThan(0);   // 블런더 희생도 가끔 나온다
  });

  it("손해가 작은 정확한 희생은 boldness 0 이어도 보통 후보처럼 둘 수 있다", () => {
    const pool = [entry("best", 0), entry("goodSac", SOUND_LOSS - 10, piece, true)];
    expect(frequency(pool, { humanness: 0.5, boldness: 0 })("goodSac")).toBeGreaterThan(0.1);
  });

  it("짧은 메이트는 놓치지 않는다", () => {
    const mate: PoolEntry = { uci: "mate", san: "mate", score: 100000 - 200, loss: 0, sacrifice: piece, speculative: false };
    const pool = [mate, entry("other", 99000), entry("sac", 99500, piece, true)];
    const f = frequency(pool, { humanness: 1, boldness: 1 }, 500);
    expect(f("mate")).toBe(1);
  });

  it("같은 시드는 같은 선택", () => {
    const pool = [entry("a", 0), entry("b", 20), entry("c", 60)];
    const run = () => { const r = mulberry32(7); return Array.from({ length: 20 }, () => chooseMove(pool, DEFAULT_PARAMS, r)!.uci); };
    expect(run()).toEqual(run());
  });
});

describe("classify", () => {
  const sac = (kind: "pawn" | "exchange" | "piece" | "major") => ({ swing: -300, kind });
  const e = (loss: number, s: PoolEntry["sacrifice"]): PoolEntry => ({ uci: "x", san: "x", score: 0, loss, sacrifice: s, speculative: false });

  it("희생 + 손해 작음 = 탁월(!!), 폰 갬빗은 !", () => {
    expect(classify(e(0, sac("piece"))).tag).toBe("brilliant");
    expect(classify(e(SOUND_LOSS, sac("major"))).symbol).toBe("!!");
    expect(classify(e(10, sac("pawn"))).tag).toBe("gambit");
  });

  it("희생 + 손해 중간 = 도박성(?!), 손해 큼 = 블런더(??)", () => {
    expect(classify(e(100, sac("piece"))).symbol).toBe("?!");
    expect(classify(e(151, sac("piece"))).symbol).toBe("??");
    expect(classify(e(700, sac("major"))).tag).toBe("blunder");
  });

  it("희생이 아닌 수는 큰 손해일 때만 실수로 표시", () => {
    expect(classify(e(20, null)).tag).toBe("normal");
    expect(classify(e(200, null))).toMatchObject({ tag: "mistake", symbol: "?" });
  });
});
