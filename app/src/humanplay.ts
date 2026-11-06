// 인간 모드로 엔진 차례의 수를 고르는 과정:
//  1) 엔진(MultiPV)으로 상위 후보 수와 점수를 구한다
//  2) boldness 가 있으면, 상위 후보에 없는 "희생 수"를 몇 개 골라 searchmoves 로 따로 평가한다
//  3) 후보 전체에서 사람처럼 하나를 고르고, 둔 수가 탁월한 희생인지 블런더인지 분류한다

import { Chess } from "chess.js";
import type { SearchInfo, UciEngine } from "./engine/uci";
import { LineCollector } from "./strategy";
import {
  chooseMove, classify, enumerateSacrifices, makePool,
  type HumanParams, type LegalSacrifice, type MoveReport, type PoolEntry, type Rng,
} from "./humanmode";

export interface HumanPlayOptions {
  /** 상위 후보(MultiPV)를 분석하는 시간(ms) */
  topMs?: number;
  /** 희생 수 하나를 따로 평가하는 시간(ms) */
  probeMs?: number;
  /** 따로 평가할 희생 수의 최대 개수 */
  maxProbes?: number;
  topLines?: number;
  rng?: Rng;
  /** 분석이 끝난 뒤 엔진에 되돌릴 Skill Level */
  skillRestore(): number;
  /** true 를 돌려주면(새 게임·무르기 등) 남은 단계를 건너뛴다 */
  cancelled?(): boolean;
  /** 1순위 라인 진행 상황(평가 막대용) */
  onBest?(info: SearchInfo): void;
}

export interface HumanChoice {
  entry: PoolEntry;
  report: MoveReport;
  /** 엔진이 보기에 가장 좋은 후보 */
  best: PoolEntry;
  pool: PoolEntry[];
}

/** 가치가 큰 희생, 체크·포획이 섞인 희생을 더 "해 보고 싶은" 것으로 본다. 중복 없이 확률적으로 고른다. */
export function pickProbes(candidates: LegalSacrifice[], count: number, rng: Rng): LegalSacrifice[] {
  const pool = [...candidates];
  const out: LegalSacrifice[] = [];
  const weight = (s: LegalSacrifice) => (1 + -s.info.swing / 600) * (s.check ? 2 : 1) * (s.capture ? 1.5 : 1);
  while (out.length < count && pool.length > 0) {
    const ws = pool.map(weight);
    let r = rng() * ws.reduce((a, c) => a + c, 0);
    let idx = 0;
    for (; idx < pool.length - 1; idx++) {
      r -= ws[idx];
      if (r <= 0) break;
    }
    out.push(pool.splice(idx, 1)[0]);
  }
  return out;
}

export async function pickHumanMove(
  engine: UciEngine,
  uciMoves: string[],
  fen: string,
  params: HumanParams,
  opt: HumanPlayOptions,
): Promise<HumanChoice | null> {
  const rng = opt.rng ?? Math.random;
  const legal = new Chess(fen).moves({ verbose: true });
  if (legal.length === 0) return null;
  const skill = { analysis: 20, restore: opt.skillRestore() };

  // 1) 상위 후보
  const top = new LineCollector();
  await engine.analyze(
    null,
    uciMoves,
    { multiPv: Math.min(opt.topLines ?? 6, legal.length), movetime: opt.topMs ?? 1000, skill },
    (i) => {
      top.update(i);
      if (i.multipv === 1) opt.onBest?.(i);
    },
  ).done;
  if (opt.cancelled?.()) return null;
  const lines = top.lines();
  if (lines.length === 0) return null;

  // 2) 희생 수: 상위 후보 밖의 것을 몇 개 따로 평가한다 (블런더급 희생은 상위 후보에 거의 나오지 않기 때문)
  const sacrifices = enumerateSacrifices(fen);
  const extra: { info: SearchInfo; uci: string }[] = [];
  if (params.boldness > 0) {
    const inTop = new Set(lines.map((l) => l.pv[0]));
    const probes = pickProbes(sacrifices.filter((s) => !inTop.has(s.uci)), opt.maxProbes ?? 3, rng);
    for (const p of probes) {
      if (opt.cancelled?.()) return null;
      let info: SearchInfo | null = null;
      await engine.analyze(
        null,
        uciMoves,
        { multiPv: 1, searchMoves: [p.uci], movetime: opt.probeMs ?? 250, skill },
        (i) => {
          if (i.multipv === 1 && i.pv.length > 0) info = i;
        },
      ).done;
      if (info) extra.push({ info, uci: p.uci });
    }
  }
  if (opt.cancelled?.()) return null;

  // 3) 선택과 분류
  const pool = makePool(fen, lines, extra, sacrifices);
  const entry = chooseMove(pool, params, rng);
  if (!entry) return null;
  const best = pool.reduce((a, c) => (c.score > a.score ? c : a));
  return { entry, report: classify(entry), best, pool };
}
