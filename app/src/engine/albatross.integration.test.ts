// 실제 Albatross 실행 파일로 분석 API(MultiPV, searchmoves)를 검증한다.
// 바이너리 경로가 있을 때만 실행된다:  ALBATROSS_BIN=../engine/build/albatross npm test
import { spawn } from "node:child_process";
import { createInterface } from "node:readline";
import { describe, expect, it } from "vitest";
import { UciEngine, type SearchInfo, type Transport } from "./uci";
import { LineCollector } from "../strategy";

const BIN = process.env.ALBATROSS_BIN;

function processTransport(bin: string): Transport {
  const p = spawn(bin, [], { stdio: ["pipe", "pipe", "ignore"] });
  let cb: (l: string) => void = () => {};
  createInterface({ input: p.stdout }).on("line", (l) => cb(l.trim()));
  return { send: (c) => p.stdin.write(c + "\n"), onLine: (f) => (cb = f), terminate: () => p.kill() };
}

describe.skipIf(!BIN)("Albatross 와의 분석 API", () => {
  it("MultiPV 3 은 서로 다른 첫 수 3개를 돌려준다", async () => {
    const engine = new UciEngine(processTransport(BIN!));
    await engine.init();
    const c = new LineCollector();
    await engine.analyze(null, [], { multiPv: 3, movetime: 300 }, (i) => c.update(i)).done;
    const lines = c.lines();
    expect(lines).toHaveLength(3);
    expect(new Set(lines.map((l) => l.pv[0])).size).toBe(3);
    // 분석이 끝나면 MultiPV 가 1 로 돌아가 일반 탐색이 가능하다
    expect(await engine.bestMove(null, [], { movetime: 100 })).toMatch(/^[a-h][1-8][a-h][1-8]/);
    engine.quit();
  });

  it("searchmoves 는 movetime 뒤에 붙여도 그 수만 평가한다", async () => {
    const engine = new UciEngine(processTransport(BIN!));
    await engine.init();
    let last: SearchInfo | null = null;
    await engine.analyze(null, [], { multiPv: 1, movetime: 300, searchMoves: ["a2a3"] }, (i) => (last = i)).done;
    expect(last).not.toBeNull();
    expect((last as unknown as SearchInfo).pv[0]).toBe("a2a3");
    engine.quit();
  });

  it("분석 도중 stop() 하면 곧바로 엔진을 다시 쓸 수 있다", async () => {
    const engine = new UciEngine(processTransport(BIN!));
    await engine.init();
    const h = engine.analyze(null, ["e2e4"], { multiPv: 2, movetime: 60000 }, () => {});
    await new Promise((r) => setTimeout(r, 150));
    const t0 = Date.now();
    await h.stop();
    expect(Date.now() - t0).toBeLessThan(2000);
    expect(await engine.bestMove(null, ["e2e4"], { movetime: 100 })).toMatch(/^[a-h][1-8][a-h][1-8]/);
    engine.quit();
  });
});
