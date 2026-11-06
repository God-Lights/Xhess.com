import { describe, expect, it } from "vitest";
import type { DrawShape } from "chessground/draw";
import type { SearchInfo } from "./engine/uci";
import {
  LineCollector, buildPlanShapes, buildShapes, describeLines, formatScore, judge,
  legalSequence, makePlanReport, planFromShapes, scoreValue,
} from "./strategy";

const START = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
const info = (multipv: number, cp: number, pv: string[], depth = 10): SearchInfo => ({ multipv, depth, cp, pv });

describe("점수", () => {
  it("메이트는 항상 센티폰보다 크고 빠른 메이트일수록 좋다", () => {
    const order = [
      { mate: 1 }, { mate: 3 }, { cp: 900 }, { cp: 0 }, { cp: -900 }, { mate: -3 }, { mate: -1 },
    ].map(scoreValue);
    expect([...order].sort((a, b) => b - a)).toEqual(order);
  });

  it("표시 형식", () => {
    expect(formatScore({ cp: 35 })).toBe("+0.35");
    expect(formatScore({ cp: -120 })).toBe("-1.20");
    expect(formatScore({ cp: 0 })).toBe("+0.00");
    expect(formatScore({ mate: 3 })).toBe("M3");
    expect(formatScore({ mate: -2 })).toBe("-M2");
  });
});

describe("LineCollector", () => {
  it("라인별로 가장 깊은 정보만 유지하고 번호순으로 돌려준다", () => {
    const c = new LineCollector();
    c.update(info(2, 10, ["d2d4"], 8));
    c.update(info(1, 30, ["e2e4"], 8));
    c.update(info(1, 35, ["e2e4"], 9));
    c.update(info(1, 99, ["a2a3"], 7)); // 더 얕은 정보는 무시
    c.update(info(3, 5, [], 9));         // PV 가 비어 있으면 무시
    expect(c.lines().map((l) => [l.multipv, l.cp, l.depth])).toEqual([[1, 35, 9], [2, 10, 8]]);
    c.clear();
    expect(c.lines()).toEqual([]);
  });
});

describe("legalSequence", () => {
  it("합법 수를 순서대로 적용하고 불법 수에서 멈춘다", () => {
    const seq = legalSequence(START, ["e2e4", "e7e5", "e2e4", "g1f3"]);
    expect(seq.map((m) => m.san)).toEqual(["e4", "e5"]);
    expect(legalSequence(START, ["e2e4", "e7e5", "g1f3"], 2)).toHaveLength(2);
  });

  it("캐슬링과 승급 표기", () => {
    const castle = legalSequence("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", ["e1g1"]);
    expect(castle[0].san).toBe("O-O");
    const promo = legalSequence("8/P6k/8/8/8/8/8/K7 w - - 0 1", ["a7a8q"]);
    expect(promo[0].san).toBe("a8=Q");
  });
});

describe("buildShapes", () => {
  const lines = [
    info(1, 35, ["e2e4", "e7e5", "g1f3"]),
    info(2, 20, ["d2d4", "d7d5"]),
    info(3, 10, ["g1f3", "g8f6"]),
  ];

  it("1순위는 내 수→예상 응수→다음 수, 2·3순위는 첫 수만", () => {
    const shapes = buildShapes(START, lines);
    expect(shapes.map((s) => [s.orig, s.dest, s.brush])).toEqual([
      ["e2", "e4", "green"], ["e7", "e5", "red"], ["g1", "f3", "paleGreen"],
      ["d2", "d4", "blue"],
      ["g1", "f3", "yellow"],
    ]);
    expect(shapes[0].label?.text).toBe("1"); // 순위 번호
    expect(shapes[1].label).toBeUndefined();
    expect(shapes[3].label?.text).toBe("2");
    expect(shapes[4].label?.text).toBe("3");
    // 가장 굵은 화살표는 내 첫 수
    expect(shapes[0].modifiers?.lineWidth).toBeGreaterThan(shapes[1].modifiers?.lineWidth ?? 99);
  });

  it("낡거나 불법인 PV 는 합법인 부분까지만 그린다", () => {
    const shapes = buildShapes(START, [info(1, 0, ["e2e4", "e2e4", "g1f3"])]);
    expect(shapes).toHaveLength(1);
    expect(buildShapes(START, [info(1, 0, ["e2e5"])])).toEqual([]);
    expect(buildShapes(START, [])).toEqual([]);
  });
});

describe("describeLines", () => {
  it("SAN 과 이어지는 수순", () => {
    const d = describeLines(START, [info(1, 35, ["e2e4", "e7e5", "g1f3", "b8c6", "f1b5", "a7a6"]), info(2, -5, ["d2d4"])]);
    expect(d[0]).toMatchObject({ rank: 1, san: "e4", score: "+0.35", continuation: ["e5", "Nf3", "Nc6", "Bb5"] });
    expect(d[1]).toMatchObject({ rank: 2, san: "d4", continuation: [] });
  });
});

describe("planFromShapes", () => {
  const arrow = (orig: string, dest?: string): DrawShape => ({ orig: orig as DrawShape["orig"], dest: dest as DrawShape["dest"] });

  it("순서대로 합법인 화살표만 계획이 된다", () => {
    const plan = planFromShapes(START, [arrow("e2", "e4"), arrow("e7", "e5"), arrow("a1", "a5"), arrow("d2", "d4")]);
    expect(plan.moves.map((m) => m.san)).toEqual(["e4", "e5", "d4"]);
    expect(plan.ignored).toBe(1); // a1→a5 는 불법
  });

  it("칸 표시(원)는 무시하고, 첫 화살표가 불법이면 계획이 비어 있다", () => {
    expect(planFromShapes(START, [arrow("e4")]).moves).toEqual([]);
    expect(planFromShapes(START, [arrow("e7", "e5")]).moves).toEqual([]); // 흑 수는 백 차례에 둘 수 없다
  });

  it("승급은 퀸으로 가정한다", () => {
    const plan = planFromShapes("8/P6k/8/8/8/8/8/K7 w - - 0 1", [arrow("a7", "a8")]);
    expect(plan.moves[0].uci).toBe("a7a8q");
  });
});

describe("judge", () => {
  it("손해 크기에 따른 판정", () => {
    expect(judge(0).verdict).toBe("best");
    expect(judge(15).verdict).toBe("best");
    expect(judge(40).verdict).toBe("good");
    expect(judge(100).verdict).toBe("inaccuracy");
    expect(judge(250).verdict).toBe("mistake");
    expect(judge(900).verdict).toBe("blunder");
  });
});

describe("makePlanReport / buildPlanShapes", () => {
  const best = info(1, 40, ["e2e4", "e7e5"]);
  const planInfo = info(1, -20, ["a2a3", "e7e5", "e2e4"]);
  const arrow = (o: string, d: string): DrawShape => ({ orig: o as DrawShape["orig"], dest: d as DrawShape["dest"] });

  it("손해와 판정, 예상 응수", () => {
    const plan = planFromShapes(START, [arrow("a2", "a3")]);
    const r = makePlanReport(START, plan, planInfo, best);
    expect(r).toMatchObject({ san: "a3", score: "-0.20", bestSan: "e4", bestScore: "+0.40", loss: 60, verdict: "good", expectedReplySan: "e5" });
    expect(r.replyMatches).toBeUndefined(); // 사용자가 응수를 그리지 않음
    expect(r.continuation).toEqual(["e5", "e4"]);
  });

  it("내가 예상한 상대 응수가 엔진 예상과 같은지 비교한다", () => {
    const same = planFromShapes(START, [arrow("a2", "a3"), arrow("e7", "e5")]);
    expect(makePlanReport(START, same, planInfo, best).replyMatches).toBe(true);
    const other = planFromShapes(START, [arrow("a2", "a3"), arrow("d7", "d5")]);
    expect(makePlanReport(START, other, planInfo, best).replyMatches).toBe(false);
  });

  it("손해는 음수가 되지 않고, 최선보다 좋게 나오면 0", () => {
    const plan = planFromShapes(START, [arrow("a2", "a3")]);
    expect(makePlanReport(START, plan, info(1, 80, ["a2a3"]), best).loss).toBe(0);
  });

  it("계획 평가 중 도형: 엔진 최선(얇게) + 예상 응수 + 다음 수", () => {
    const shapes = buildPlanShapes(START, planInfo, best);
    expect(shapes.map((s) => [s.orig, s.dest, s.brush])).toEqual([
      ["e2", "e4", "paleGreen"], ["e7", "e5", "red"], ["e2", "e4", "paleBlue"],
    ]);
    expect(buildPlanShapes(START, planInfo, undefined)).toHaveLength(2);
  });
});
