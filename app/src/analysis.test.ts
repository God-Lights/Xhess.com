import { describe, expect, it } from "vitest";
import { Analyzer } from "./analysis";
import { UciEngine, type SearchInfo, type Transport } from "./engine/uci";
import { planFromShapes, type PlanReport } from "./strategy";

const START = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

/**
 * 시작 국면만 아는 가짜 UCI 엔진: MultiPV/searchmoves 를 흉내 내고, go 후 짧게 정보를 내보낸 뒤
 * stop 이 오거나 시간이 지나면 bestmove 를 보낸다. 받은 명령을 모두 기록한다.
 */
function fakeEngine() {
  const received: string[] = [];
  let cb: (l: string) => void = () => {};
  let multiPv = 1;
  let timer: ReturnType<typeof setTimeout> | null = null;
  let searching = false;
  let best = "e2e4";

  const table: Record<string, { cp: number; pv: string[] }> = {
    e2e4: { cp: 40, pv: ["e2e4", "e7e5", "g1f3"] },
    d2d4: { cp: 30, pv: ["d2d4", "d7d5", "c2c4"] },
    g1f3: { cp: 25, pv: ["g1f3", "g8f6", "d2d4"] },
    a2a3: { cp: -20, pv: ["a2a3", "e7e5", "e2e4"] },
  };

  const finish = () => {
    if (!searching) return;
    searching = false;
    if (timer) clearTimeout(timer);
    cb(`bestmove ${best}`);
  };

  const t: Transport = {
    send(cmd) {
      received.push(cmd);
      if (cmd === "uci") cb("uciok");
      else if (cmd === "isready") cb("readyok");
      else if (cmd.startsWith("setoption name MultiPV")) multiPv = Number(cmd.split(" ").pop());
      else if (cmd.startsWith("go")) {
        const sm = /searchmoves ((?:[a-h][1-8][a-h][1-8][qrbn]? ?)+)/.exec(cmd)?.[1].trim().split(" ");
        const moves = (sm ?? Object.keys(table)).slice(0, multiPv);
        best = moves[0];
        searching = true;
        moves.forEach((m, i) => cb(`info depth 12 seldepth 14 multipv ${i + 1} score cp ${table[m].cp} nodes 1000 pv ${table[m].pv.join(" ")}`));
        timer = setTimeout(finish, 60); // 정해진 시간 후 스스로 종료
      } else if (cmd === "stop") setTimeout(finish, 1);
    },
    onLine: (f) => (cb = f),
    terminate() {},
  };
  return { transport: t, received };
}

const make = async () => {
  const f = fakeEngine();
  const engine = new UciEngine(f.transport);
  await engine.init();
  return { engine, received: f.received };
};

describe("UciEngine.analyze", () => {
  it("MultiPV 와 searchmoves 를 보내고, 끝나면 옵션을 되돌린다", async () => {
    const { engine, received } = await make();
    const infos: SearchInfo[] = [];
    const h = engine.analyze(null, ["e2e4"], { multiPv: 3, movetime: 500, skill: { analysis: 20, restore: 7 } }, (i) => infos.push(i));
    await h.done;
    expect(received).toContain("setoption name MultiPV value 3");
    expect(received).toContain("setoption name Skill Level value 20");
    expect(received.some((c) => c.startsWith("position startpos moves e2e4"))).toBe(true);
    expect(received.some((c) => c === "go movetime 500")).toBe(true);
    expect(infos.map((i) => i.multipv)).toEqual([1, 2, 3]);
    // 끝난 뒤: MultiPV 1, Skill Level 복원
    expect(received.at(-2)).toBe("setoption name MultiPV value 1");
    expect(received.at(-1)).toBe("setoption name Skill Level value 7");
  });

  it("stop() 은 bestmove 와 옵션 복원이 끝난 뒤에 이행된다", async () => {
    const { engine, received } = await make();
    const h = engine.analyze(null, [], { multiPv: 1, movetime: 10000, searchMoves: ["d2d4", "a2a3"] }, () => {});
    expect(received.some((c) => c === "go movetime 10000 searchmoves d2d4 a2a3")).toBe(true);
    await h.stop();
    expect(received.at(-1)).toBe("setoption name MultiPV value 1");
    // 엔진은 곧바로 대국용 탐색에 쓸 수 있고, 분석용 searchmoves 제한은 다음 탐색에 남지 않는다
    const best = await engine.bestMove(null, [], { movetime: 50 });
    expect(best).toBe("e2e4");
    expect(received.at(-1)).toBe("go movetime 50");
  });
});

describe("Analyzer", () => {
  const callbacks = () => {
    const calls = { lines: [] as { n: number; fen: string; final: boolean }[], plans: [] as PlanReport[] };
    return {
      calls,
      cb: {
        onLines: (lines: SearchInfo[], fen: string, final: boolean) => calls.lines.push({ n: lines.length, fen, final }),
        onPlan: (r: PlanReport) => calls.plans.push(r),
      },
    };
  };

  it("추천 분석은 후보 3개를 만들고 마지막에 final 로 알린다", async () => {
    const { engine } = await make();
    const { calls, cb } = callbacks();
    const a = new Analyzer(() => engine, cb, { skillRestore: () => 8, recommendMs: 100, throttleMs: 0 });
    await a.recommend([], START);
    expect(calls.lines.at(-1)).toEqual({ n: 3, fen: START, final: true });
    expect(calls.lines.slice(0, -1).every((c) => !c.final)).toBe(true);
  });

  it("취소하면 늦게 도착한 결과를 알리지 않고 엔진을 다시 쓸 수 있게 한다", async () => {
    const { engine, received } = await make();
    const { calls, cb } = callbacks();
    const a = new Analyzer(() => engine, cb, { skillRestore: () => 8, recommendMs: 5000, throttleMs: 0 });
    const running = a.recommend([], START);
    await new Promise((r) => setTimeout(r, 5));
    const before = calls.lines.length;
    await a.cancel();
    await running;
    expect(calls.lines.some((c) => c.final)).toBe(false); // 취소된 분석은 final 을 알리지 않는다
    expect(calls.lines.length).toBe(before);
    expect(received.at(-1)).toBe("setoption name Skill Level value 8");
    expect(await engine.bestMove(null, [], { movetime: 50 })).toBe("e2e4");
  });

  it("새 분석을 시작하면 이전 분석이 먼저 멈춘다 (동시에 두 개가 돌지 않는다)", async () => {
    const { engine, received } = await make();
    const { cb } = callbacks();
    const a = new Analyzer(() => engine, cb, { skillRestore: () => 8, recommendMs: 5000, throttleMs: 0 });
    const first = a.recommend([], START);
    await new Promise((r) => setTimeout(r, 5));
    const second = a.recommend(["e2e4"], "x"); // 새 국면
    await Promise.all([first, second]);
    const goIdx = received.map((c, i) => (c.startsWith("go") ? i : -1)).filter((i) => i >= 0);
    const stopIdx = received.indexOf("stop");
    expect(goIdx).toHaveLength(2);
    expect(stopIdx).toBeGreaterThan(goIdx[0]);
    expect(stopIdx).toBeLessThan(goIdx[1]); // 첫 go 와 둘째 go 사이에 stop
  });

  it("계획 평가: 엔진 1순위와 비교한 보고서", async () => {
    const { engine } = await make();
    const { calls, cb } = callbacks();
    const a = new Analyzer(() => engine, cb, { skillRestore: () => 8, recommendMs: 100, planMs: 100, throttleMs: 0 });
    await a.recommend([], START); // 1순위: e2e4 (+0.40) 확보
    const plan = planFromShapes(START, [{ orig: "a2", dest: "a3" }]);
    await a.evaluatePlan([], START, plan);
    expect(calls.plans).toHaveLength(1);
    expect(calls.plans[0]).toMatchObject({ san: "a3", score: "-0.20", bestSan: "e4", bestScore: "+0.40", loss: 60, verdict: "good" });
  });

  it("분석 도중 취소돼도 이미 구한 1순위를 비교 기준으로 재사용한다 (재탐색 없음)", async () => {
    const { engine, received } = await make();
    const { calls, cb } = callbacks();
    const a = new Analyzer(() => engine, cb, { skillRestore: () => 8, recommendMs: 5000, planMs: 100, throttleMs: 0 });
    const running = a.recommend([], START);
    await new Promise((r) => setTimeout(r, 5)); // 가짜 엔진이 깊이 12 정보를 이미 보낸 뒤
    await a.evaluatePlan([], START, planFromShapes(START, [{ orig: "a2", dest: "a3" }]));
    await running;
    expect(received.filter((c) => c.startsWith("go"))).toHaveLength(2); // 추천 1회 + 계획 1회 (1순위 재탐색 없음)
    expect(calls.plans[0]).toMatchObject({ bestSan: "e4", bestScore: "+0.40" });
  });

  it("1순위가 없으면 먼저 짧게 구한 뒤 계획을 평가한다", async () => {
    const { engine, received } = await make();
    const { calls, cb } = callbacks();
    const a = new Analyzer(() => engine, cb, { skillRestore: () => 8, planMs: 100, throttleMs: 0 });
    await a.evaluatePlan([], START, planFromShapes(START, [{ orig: "a2", dest: "a3" }]));
    expect(received.filter((c) => c.startsWith("go"))).toHaveLength(2);
    expect(calls.plans[0].bestSan).toBe("e4");
  });

  it("엔진이 없으면 아무 일도 하지 않는다", async () => {
    const { cb, calls } = callbacks();
    const a = new Analyzer(() => null, cb, { skillRestore: () => 8 });
    await a.recommend([], START);
    await a.evaluatePlan([], START, planFromShapes(START, [{ orig: "a2", dest: "a3" }]));
    expect(calls.lines).toEqual([]);
    expect(calls.plans).toEqual([]);
  });
});
