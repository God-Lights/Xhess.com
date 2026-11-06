import { describe, it, expect } from "vitest";
import { Game } from "./game";
import { parseInfo, parseBestMove } from "./engine/uci";

describe("Game", () => {
  it("시작 포지션에서 20개의 합법 수", () => {
    const g = new Game();
    let n = 0;
    for (const to of g.dests().values()) n += to.length;
    expect(n).toBe(20);
  });

  it("UCI 수 적용, 승급 판정, undo", () => {
    const g = new Game();
    expect(g.moveUci("e2e4")).not.toBeNull();
    expect(g.moveUci("e2e4")).toBeNull();
    expect(g.turn).toBe("black");
    g.undo();
    expect(g.turn).toBe("white");
    expect(g.uciMoves).toEqual([]);
  });

  it("fool's mate 는 체크메이트", () => {
    const g = new Game();
    for (const m of ["f2f3", "e7e5", "g2g4", "d8h4"]) g.moveUci(m);
    expect(g.outcome().over).toBe(true);
    expect(g.outcome().text).toContain("흑 승리");
  });
});

describe("UCI parsing", () => {
  it("info 라인", () => {
    const i = parseInfo("info depth 12 seldepth 18 multipv 1 score cp 34 nodes 1 pv e2e4 e7e5");
    expect(i).toEqual({ depth: 12, cp: 34, pv: ["e2e4", "e7e5"] });
    expect(parseInfo("info depth 3 score mate -2 pv a1a2")?.mate).toBe(-2);
    expect(parseInfo("info string hello")).toBeNull();
  });
  it("bestmove", () => {
    expect(parseBestMove("bestmove e2e4 ponder e7e5")).toBe("e2e4");
    expect(parseBestMove("bestmove (none)")).toBeNull();
  });
});
