// 전략 마킹의 순수 로직: 엔진 분석(SearchInfo)을 보드 도형과 설명 문구로, 사용자가 그린 화살표를 "계획"으로 바꾼다.
// DOM 이나 엔진에 의존하지 않으므로 단독으로 테스트할 수 있다.

import { Chess, type Square } from "chess.js";
import type { DrawShape } from "chessground/draw";
import type { SearchInfo } from "./engine/uci";

/** 점수 비교용 값(센티폰). 메이트는 매우 큰 값이며 더 빠른 메이트일수록 크다. 엔진(수를 둘 쪽) 관점 그대로. */
export function scoreValue(i: Pick<SearchInfo, "cp" | "mate">): number {
  if (i.mate !== undefined) return i.mate > 0 ? 100000 - i.mate * 100 : -100000 - i.mate * 100;
  return i.cp ?? 0;
}

/** "+0.35", "-1.20", "M3", "-M2" (수를 둘 쪽 관점) */
export function formatScore(i: Pick<SearchInfo, "cp" | "mate">): string {
  if (i.mate !== undefined) return i.mate > 0 ? `M${i.mate}` : `-M${-i.mate}`;
  const v = (i.cp ?? 0) / 100;
  return `${v >= 0 ? "+" : ""}${v.toFixed(2)}`;
}

/** MultiPV 라인별로 가장 최근(가장 깊은) 정보만 모은다. */
export class LineCollector {
  private byIndex = new Map<number, SearchInfo>();

  update(info: SearchInfo): void {
    if (info.pv.length === 0) return;
    const prev = this.byIndex.get(info.multipv);
    if (!prev || info.depth >= prev.depth) this.byIndex.set(info.multipv, info);
  }

  /** 점수가 높은 순(같으면 라인 번호순). MultiPV 라인은 따로 탐색되어 번호와 점수 순서가 어긋날 수 있다. */
  lines(): SearchInfo[] {
    return [...this.byIndex.entries()]
      .sort((a, b) => scoreValue(b[1]) - scoreValue(a[1]) || a[0] - b[0])
      .map(([, v]) => v);
  }

  clear(): void {
    this.byIndex.clear();
  }
}

export interface PlayedMove {
  uci: string;
  san: string;
  from: Square;
  to: Square;
}

/** fen 에서 시작해 UCI 수열을 순서대로 적용한다. 불법인 수를 만나면 거기서 멈춘다(오래된 PV 방어). */
export function legalSequence(fen: string, uciMoves: string[], max = Infinity): PlayedMove[] {
  const chess = new Chess(fen);
  const out: PlayedMove[] = [];
  for (const uci of uciMoves.slice(0, max)) {
    try {
      const m = chess.move({ from: uci.slice(0, 2) as Square, to: uci.slice(2, 4) as Square, promotion: uci[4] });
      out.push({ uci, san: m.san, from: m.from, to: m.to });
    } catch {
      break;
    }
  }
  return out;
}

const CANDIDATE_BRUSH = ["green", "blue", "yellow"];

/**
 * 엔진 추천 도형.
 *  1순위 라인: 내 수(초록, 굵게, 순위 라벨) → 예상 응수(빨강) → 내 다음 수(연한 초록)
 *  2·3순위 라인: 첫 수만 파랑/노랑 화살표와 순위 라벨
 */
export function buildShapes(fen: string, lines: SearchInfo[]): DrawShape[] {
  const shapes: DrawShape[] = [];
  lines.slice(0, 3).forEach((line, idx) => {
    const seq = legalSequence(fen, line.pv, idx === 0 ? 3 : 1);
    seq.forEach((m, k) => {
      const brush = idx === 0 ? ["green", "red", "paleGreen"][k] : CANDIDATE_BRUSH[idx];
      const width = idx === 0 ? [11, 8, 6][k] : 6;
      shapes.push({
        orig: m.from,
        dest: m.to,
        brush,
        modifiers: { lineWidth: width },
        // 라벨은 작아서 점수 대신 순위 번호를 붙인다. 패널의 "1. e4 +0.26" 목록과 짝이 된다.
        ...(k === 0 ? { label: { text: String(idx + 1) } } : {}),
      });
    });
  });
  return shapes;
}

export interface CandidateText {
  rank: number;
  san: string;
  score: string;
  /** 이어지는 예상 수순(SAN), 첫 수 제외 최대 4수 */
  continuation: string[];
  depth: number;
}

export function describeLines(fen: string, lines: SearchInfo[]): CandidateText[] {
  return lines.slice(0, 3).flatMap((line, idx) => {
    const seq = legalSequence(fen, line.pv, 5);
    if (seq.length === 0) return [];
    return [{
      rank: idx + 1,
      san: seq[0].san,
      score: formatScore(line),
      continuation: seq.slice(1).map((m) => m.san),
      depth: line.depth,
    }];
  });
}

/** 사용자가 보드에 그린 화살표 → 순서대로 합법인 수만 이어 붙인 "계획". 합법이 아닌 화살표는 무시한다. */
export interface Plan {
  moves: PlayedMove[];
  ignored: number;
}

export function planFromShapes(fen: string, shapes: readonly DrawShape[]): Plan {
  const chess = new Chess(fen);
  const moves: PlayedMove[] = [];
  let ignored = 0;
  for (const s of shapes) {
    if (!s.dest) continue; // 칸 표시(원)는 계획이 아니다
    try {
      const m = chess.move({ from: s.orig as Square, to: s.dest as Square, promotion: "q" });
      moves.push({ uci: m.from + m.to + (m.promotion ?? ""), san: m.san, from: m.from, to: m.to });
    } catch {
      ignored++;
    }
  }
  return { moves, ignored };
}

export type Verdict = "best" | "good" | "inaccuracy" | "mistake" | "blunder";

export function judge(lossCp: number): { verdict: Verdict; text: string } {
  if (lossCp <= 15) return { verdict: "best", text: "최선에 가까운 수" };
  if (lossCp <= 60) return { verdict: "good", text: "좋은 수" };
  if (lossCp <= 150) return { verdict: "inaccuracy", text: "부정확한 수" };
  if (lossCp <= 300) return { verdict: "mistake", text: "실수" };
  return { verdict: "blunder", text: "큰 실수" };
}

export interface PlanReport {
  uci: string;
  san: string;
  score: string;
  bestSan: string;
  bestScore: string;
  /** 엔진 최선 대비 손해(센티폰, 0 이상) */
  loss: number;
  verdict: Verdict;
  verdictText: string;
  /** 엔진이 예상하는 상대 응수 */
  expectedReplySan?: string;
  /** 사용자가 두 번째 화살표로 예상 응수를 그렸을 때만: 엔진 예상과 같은가 */
  replyMatches?: boolean;
  continuation: string[];
}

/**
 * planInfo: searchmoves 로 내 계획의 첫 수만 탐색한 결과, bestInfo: 같은 국면의 엔진 1순위 라인.
 * 둘 다 같은 국면(수를 둘 쪽 관점)에서 얻은 점수여야 비교할 수 있다.
 */
export function makePlanReport(fen: string, plan: Plan, planInfo: SearchInfo, bestInfo: SearchInfo): PlanReport {
  const seq = legalSequence(fen, planInfo.pv, 4);
  const best = legalSequence(fen, bestInfo.pv, 1);
  const loss = Math.max(0, scoreValue(bestInfo) - scoreValue(planInfo));
  const j = judge(loss);
  const reply = seq[1];
  const userReply = plan.moves[1];
  return {
    uci: plan.moves[0].uci,
    san: plan.moves[0].san,
    score: formatScore(planInfo),
    bestSan: best[0]?.san ?? "-",
    bestScore: formatScore(bestInfo),
    loss,
    verdict: j.verdict,
    verdictText: j.text,
    expectedReplySan: reply?.san,
    replyMatches: userReply && reply ? userReply.uci.slice(0, 4) === reply.uci.slice(0, 4) : undefined,
    continuation: seq.slice(1).map((m) => m.san),
  };
}

/** 계획을 평가하는 동안 보드에 보여 줄 엔진 도형: 엔진 1순위(참고용, 얇게)와 내 계획의 예상 응수·다음 수. */
export function buildPlanShapes(fen: string, planInfo: SearchInfo, bestInfo: SearchInfo | undefined): DrawShape[] {
  const shapes: DrawShape[] = [];
  const best = bestInfo ? legalSequence(fen, bestInfo.pv, 1)[0] : undefined;
  if (best) shapes.push({ orig: best.from, dest: best.to, brush: "paleGreen", modifiers: { lineWidth: 5 }, label: { text: "1" } });
  const seq = legalSequence(fen, planInfo.pv, 3);
  if (seq[1]) shapes.push({ orig: seq[1].from, dest: seq[1].to, brush: "red", modifiers: { lineWidth: 9 } });
  if (seq[2]) shapes.push({ orig: seq[2].from, dest: seq[2].to, brush: "paleBlue", modifiers: { lineWidth: 6 } });
  return shapes;
}
