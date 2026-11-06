import { Chess } from "chess.js";
import { describe, expect, it } from "vitest";
import { UciEngine, type SearchInfo, type Transport } from "./engine/uci";
import { pickHumanMove, pickProbes } from "./humanplay";
import { enumerateSacrifices, mulberry32 } from "./humanmode";

// 1.e4 e5 2.Nf3 Nc6: 백(엔진)에게 Nxe5(f3e5) 같은 희생 수가 있다.
const MOVES = ["e2e4", "e7e5", "g1f3", "b8c6"];
const FEN = "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 0 1";

/** 점수표를 따르는 가짜 엔진. startpos + moves 로 합법 수를 계산하고 MultiPV/searchmoves 를 흉내 낸다. */
function fakeEngine(score: (uci: string) => number) {
  const received: string[] = [];
  let cb: (l: string) => void = () => {};
  let multiPv = 1;
  let position = "";
  const t: Transport = {
    send(cmd) {
      received.push(cmd);
      if (cmd === "uci") cb("uciok");
      else if (cmd === "isready") cb("readyok");
      else if (cmd.startsWith("setoption name MultiPV")) multiPv = Number(cmd.split(" ").pop());
      else if (cmd.startsWith("position")) position = cmd;
      else if (cmd.startsWith("go")) {
        const chess = new Chess();
        const mv = /moves (.*)$/.exec(position)?.[1].trim().split(/\s+/) ?? [];
        for (const m of mv) chess.move({ from: m.slice(0, 2), to: m.slice(2, 4), promotion: m[4] });
        let legal = chess.moves({ verbose: true }).map((m) => m.from + m.to + (m.promotion ?? ""));
        const sm = /searchmoves (.*)$/.exec(cmd)?.[1].trim().split(/\s+/);
        if (sm) legal = legal.filter((u) => sm.includes(u));
        const ranked = legal.sort((a, b) => score(b) - score(a)).slice(0, multiPv);
        ranked.forEach((u, i) => cb(`info depth 14 multipv ${i + 1} score cp ${score(u)} nodes 1 pv ${u}`));
        setTimeout(() => cb(`bestmove ${ranked[0] ?? "0000"}`), 1);
      }
    },
    onLine: (f) => (cb = f),
    terminate() {},
  };
  return { transport: t, received };
}

const make = async (score: (u: string) => number) => {
  const f = fakeEngine(score);
  const engine = new UciEngine(f.transport);
  await engine.init();
  return { engine, received: f.received };
};

// d2d4 이 최선(+30), 일반 수는 -10 ~ +25, 희생(Nxe5 등)은 크게 나쁘다.
const scores: Record<string, number> = { d2d4: 30, f1c4: 25, b1c3: 20, d2d3: 10, f3e5: -250 };
const score = (u: string) => scores[u] ?? (enumerateSacrifices(FEN).some((s) => s.uci === u) ? -600 : -10);

describe("pickProbes", () => {
  it("중복 없이 요청한 개수만큼, 후보가 모자라면 있는 만큼", () => {
    const sacs = enumerateSacrifices(FEN);
    const picked = pickProbes(sacs, 3, mulberry32(1));
    expect(picked).toHaveLength(Math.min(3, sacs.length));
    expect(new Set(picked.map((p) => p.uci)).size).toBe(picked.length);
    expect(pickProbes([], 3, mulberry32(1))).toEqual([]);
    expect(pickProbes(sacs, 999, mulberry32(1))).toHaveLength(sacs.length);
  });
});

describe("pickHumanMove", () => {
  const opts = (extra = {}) => ({ topMs: 10, probeMs: 10, skillRestore: () => 8, ...extra });

  it("희생 성향 0 이면 후보를 한 번만 분석하고(희생 수 탐색 없음) 최선에 가까운 수를 둔다", async () => {
    const { engine, received } = await make(score);
    const choice = await pickHumanMove(engine, MOVES, FEN, { humanness: 0, boldness: 0 }, opts({ rng: () => 0.01 }));
    expect(choice).not.toBeNull();
    expect(received.filter((c) => c.startsWith("go"))).toHaveLength(1);
    expect(received.some((c) => c.includes("searchmoves"))).toBe(false);
    expect(choice!.entry.uci).toBe("d2d4");
    expect(choice!.report.tag).toBe("normal");
    expect(choice!.best.uci).toBe("d2d4");
    // 분석 후 옵션 복원
    expect(received.at(-2)).toBe("setoption name MultiPV value 1");
    expect(received.at(-1)).toBe("setoption name Skill Level value 8");
  });

  it("희생 성향이 있으면 상위 후보 밖의 희생 수를 searchmoves 로 따로 평가한다", async () => {
    const { engine, received } = await make(score);
    const legalSacs = new Set(enumerateSacrifices(FEN).map((s) => s.uci));
    // 난수가 항상 0 이면 "희생하고 싶은 기분"이 되고 첫 대담한 후보를 고른다
    const choice = await pickHumanMove(engine, MOVES, FEN, { humanness: 0.5, boldness: 1 }, opts({ rng: () => 0 }));
    const probes = received.filter((c) => c.includes("searchmoves"));
    expect(probes.length).toBeGreaterThan(0);
    expect(probes.length).toBeLessThanOrEqual(3);
    for (const c of probes) expect(legalSacs.has(c.split("searchmoves ")[1])).toBe(true);
    expect(choice!.entry.sacrifice).not.toBeNull();
    expect(choice!.entry.speculative).toBe(true);
    expect(["blunder", "speculative"]).toContain(choice!.report.tag); // 손해가 큰 희생
    expect(choice!.entry.loss).toBeGreaterThan(100);
  });

  it("점수가 좋은 희생은 탁월한 희생(!!)으로 분류된다", async () => {
    // Nxe5 가 사실은 최선인 세계
    const good = (u: string) => (u === "f3e5" ? 80 : score(u));
    const { engine } = await make(good);
    const choice = await pickHumanMove(engine, MOVES, FEN, { humanness: 0, boldness: 0 }, opts({ rng: mulberry32(2) }));
    expect(choice!.entry.uci).toBe("f3e5");
    expect(choice!.report).toMatchObject({ tag: "brilliant", symbol: "!!" });
  });

  it("수가 없는 국면(체크메이트)에서는 null", async () => {
    const mate = ["f2f3", "e7e5", "g2g4", "d8h4"]; // 백이 체크메이트당함
    const { engine, received } = await make(() => 0);
    const fen = "rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3";
    expect(await pickHumanMove(engine, mate, fen, { humanness: 0.5, boldness: 0.5 }, opts())).toBeNull();
    expect(received.some((c) => c.startsWith("go"))).toBe(false); // 분석조차 시작하지 않는다
  });

  it("취소되면 이후 단계(희생 수 평가)를 하지 않고 null", async () => {
    const { engine, received } = await make(score);
    let calls = 0;
    const choice = await pickHumanMove(engine, MOVES, FEN, { humanness: 0.5, boldness: 1 },
      opts({ rng: mulberry32(3), cancelled: () => ++calls >= 1 }));
    expect(choice).toBeNull();
    expect(received.some((c) => c.includes("searchmoves"))).toBe(false);
  });

  it("평가 막대용 1순위 진행 정보를 알린다", async () => {
    const { engine } = await make(score);
    const seen: number[] = [];
    await pickHumanMove(engine, MOVES, FEN, { humanness: 0, boldness: 0 }, opts({ onBest: (i: SearchInfo) => seen.push(i.cp ?? 0), rng: mulberry32(1) }));
    expect(seen).toEqual([30]);
  });
});
