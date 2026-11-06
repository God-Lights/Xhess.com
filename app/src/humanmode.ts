// 인간 모드의 순수 로직: 희생(sacrifice) 판정, 후보 수 선택, 결과 분류.
// 엔진이 아니라 "엔진이 준 후보 수와 점수"를 입력으로 받으므로 어떤 UCI 엔진과도 쓸 수 있고 단독으로 테스트할 수 있다.

import { Chess, type Color, type Piece, type PieceSymbol, type Square } from "chess.js";
import { scoreValue } from "./strategy";
import type { SearchInfo } from "./engine/uci";

const VALUE: Record<PieceSymbol, number> = { p: 100, n: 320, b: 330, r: 500, q: 900, k: 0 };

export type Rng = () => number;

/** 시드 고정 난수(테스트와 재현용). 0 이상 1 미만 */
export function mulberry32(seed: number): Rng {
  let a = seed >>> 0;
  return () => {
    a = (a + 0x6d2b79f5) >>> 0;
    let t = a;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

/** 킹은 가장 마지막에 잡는 기물이라 아주 큰 값을 준다 */
const KING_ORDER = 100000;

/**
 * 한 칸 `target` 에서 `by` 쪽이 시작하는 교환(정적 교환 평가, SEE)의 순이득. 0 이상(교환을 안 하는 선택 포함).
 * 가장 값이 낮은 기물부터 번갈아 잡고, 잡힌 기물이 빠진 뒤 드러나는 슬라이더(x-ray)도 반영한다.
 * 킹은 반대편이 그 칸을 더 지키고 있지 않을 때만 잡을 수 있다. 핀은 무시한다.
 * chess.js 의 remove/put 으로 기물을 옮겨 보고, 끝나면 원래대로 되돌린다.
 */
function exchangeGain(chess: Chess, target: Square, by: Color): number {
  const victim = chess.get(target);
  if (!victim || victim.color === by) return 0;

  const touched: { sq: Square; piece: Piece | undefined }[] = [{ sq: target, piece: victim }];
  const gain: number[] = [];
  let occupant: Piece = victim;
  let side: Color = by;

  for (let depth = 0; depth < 30; depth++) {
    // 가장 값이 낮은 공격자
    let from: Square | null = null;
    let piece: Piece | undefined;
    let order = Infinity;
    for (const sq of chess.attackers(target, side)) {
      const p = chess.get(sq);
      if (!p) continue;
      const o = p.type === "k" ? KING_ORDER : VALUE[p.type];
      if (o < order) { order = o; from = sq; piece = p; }
    }
    if (!from || !piece) break;
    // 킹은 상대가 그 칸을 더 지키면 잡을 수 없다
    if (piece.type === "k" && chess.attackers(target, side === "w" ? "b" : "w").length > 0) break;

    gain[depth] = VALUE[occupant.type] - (depth > 0 ? gain[depth - 1] : 0);
    touched.push({ sq: from, piece });
    chess.remove(from);
    chess.remove(target);
    chess.put(piece, target);
    occupant = piece;
    side = side === "w" ? "b" : "w";
  }

  // 원래 배치로 복구
  for (let i = touched.length - 1; i >= 0; i--) {
    const { sq, piece } = touched[i];
    chess.remove(sq);
    if (piece) chess.put(piece, sq);
  }

  // 마지막 수부터 거슬러 올라가며 "여기서 멈추는 편이 낫다"를 반영한다.
  for (let i = gain.length - 1; i > 0; i--) gain[i - 1] = -Math.max(-gain[i - 1], gain[i]);
  return Math.max(0, gain[0] ?? 0);
}

export type SacrificeKind = "pawn" | "exchange" | "piece" | "major";

export interface SacrificeInfo {
  /** 교환이 끝난 뒤의 물질 변화(센티폰, 음수 = 내줌) */
  swing: number;
  kind: SacrificeKind;
}

export const SACRIFICE_THRESHOLD = 100; // 폰 하나 이상을 내줘야 희생으로 본다

export function kindOf(swing: number): SacrificeKind {
  const s = -swing;
  if (s < 150) return "pawn";
  if (s < 300) return "exchange";
  if (s < 600) return "piece";
  return "major";
}

export const KIND_TEXT: Record<SacrificeKind, string> = {
  pawn: "폰 갬빗",
  exchange: "교환 희생",
  piece: "기물 희생",
  major: "대형 희생",
};

/**
 * 이미 `chess` 에 수 m 을 둔 상태(상대 차례)에서 내 물질 변화를 구한다:
 *  (이 수로 잡은 값) − (상대가 한 칸에서 얻을 수 있는 최대 순이득).
 * 상대는 내 기물이 공격받는 칸 중 가장 이득인 한 곳의 교환을 고른다.
 */
function swingAfter(chess: Chess, captured: PieceSymbol | undefined, promotion: PieceSymbol | undefined): number {
  const me: Color = chess.turn() === "w" ? "b" : "w";
  const opponent = chess.turn();
  let gained = captured ? VALUE[captured] : 0;
  if (promotion) gained += VALUE[promotion] - VALUE.p;
  let best = 0;

  if (chess.inCheck()) {
    // 체크를 받은 쪽은 체크를 풀어야 하므로, 다른 곳의 포획은 할 수 없다: 합법 응수 중 포획만 본다.
    for (const r of chess.moves({ verbose: true })) {
      if (!r.captured) continue;
      chess.move(r);
      // 응수로 잡은 값에서, 내가 그 칸에서 되찾을 수 있는 순이득을 뺀다.
      const net = VALUE[r.captured] + (r.promotion ? VALUE[r.promotion] - VALUE.p : 0) - exchangeGain(chess, r.to, me);
      chess.undo();
      best = Math.max(best, net);
    }
    return gained - best;
  }

  for (const row of chess.board())
    for (const sq of row) {
      if (!sq || sq.color !== me || sq.type === "k") continue;
      if (!chess.isAttacked(sq.square, opponent)) continue;
      best = Math.max(best, exchangeGain(chess, sq.square, opponent));
    }
  return gained - best;
}

function toSacrifice(swing: number): SacrificeInfo | null {
  return swing <= -SACRIFICE_THRESHOLD ? { swing, kind: kindOf(swing) } : null;
}

/** 이 수가 희생이면 정보를, 아니면 null. 불법인 수는 null. */
export function sacrificeInfo(fen: string, uci: string): SacrificeInfo | null {
  const chess = new Chess(fen);
  let m;
  try {
    m = chess.move({ from: uci.slice(0, 2), to: uci.slice(2, 4), promotion: uci[4] });
  } catch {
    return null;
  }
  return toSacrifice(swingAfter(chess, m.captured, m.promotion));
}

export interface LegalSacrifice {
  uci: string;
  san: string;
  info: SacrificeInfo;
  check: boolean;
  capture: boolean;
}

/** 현재 국면에서 가능한 모든 희생 수. */
export function enumerateSacrifices(fen: string): LegalSacrifice[] {
  const chess = new Chess(fen);
  const out: LegalSacrifice[] = [];
  for (const m of chess.moves({ verbose: true })) {
    chess.move(m);
    const info = toSacrifice(swingAfter(chess, m.captured, m.promotion));
    chess.undo();
    if (info) out.push({ uci: m.from + m.to + (m.promotion ?? ""), san: m.san, info, check: m.san.includes("+") || m.san.includes("#"), capture: m.isCapture() });
  }
  return out;
}

export interface PoolEntry {
  uci: string;
  san: string;
  /** 수를 둘 쪽 관점의 비교용 점수 */
  score: number;
  /** 최선 후보와의 점수 차(0 이상) */
  loss: number;
  sacrifice: SacrificeInfo | null;
  /** 상위 후보(MultiPV)가 아니라 희생 수를 따로 평가해 넣은 것 */
  speculative: boolean;
}

export interface HumanParams {
  /** 0..1: 클수록 최선이 아닌 수를 자주 둔다(실수 빈도) */
  humanness: number;
  /** 0..1: 클수록 손해가 큰 희생도 자주 시도한다 */
  boldness: number;
}

export const DEFAULT_PARAMS: HumanParams = { humanness: 0.4, boldness: 0.5 };

/** 손해가 이 이하인 희생은 "정확한 희생"이라 보고 평소 후보와 똑같이 취급한다 */
export const SOUND_LOSS = 40;

/** 이 이하의 메이트는 사람도 놓치지 않으므로 무조건 둔다 */
const FORCED_MATE_IN = 3;

export function makePool(
  fen: string,
  lines: SearchInfo[],
  extra: { info: SearchInfo; uci: string }[],
  sacrifices: LegalSacrifice[],
): PoolEntry[] {
  const chess = new Chess(fen);
  const sacByUci = new Map(sacrifices.map((s) => [s.uci, s]));
  const raw: { uci: string; score: number; speculative: boolean }[] = [];
  const seen = new Set<string>();
  for (const l of lines) {
    const uci = l.pv[0];
    if (!uci || seen.has(uci)) continue;
    seen.add(uci);
    raw.push({ uci, score: scoreValue(l), speculative: false });
  }
  for (const e of extra) {
    if (seen.has(e.uci)) continue;
    seen.add(e.uci);
    raw.push({ uci: e.uci, score: scoreValue(e.info), speculative: true });
  }
  if (raw.length === 0) return [];
  const best = Math.max(...raw.map((r) => r.score));
  const pool: PoolEntry[] = [];
  for (const r of raw) {
    let san: string;
    try {
      san = chess.move({ from: r.uci.slice(0, 2), to: r.uci.slice(2, 4), promotion: r.uci[4] }).san;
      chess.undo();
    } catch {
      continue;
    }
    const sac = sacByUci.get(r.uci)?.info ?? null;
    pool.push({ uci: r.uci, san, score: r.score, loss: Math.max(0, best - r.score), sacrifice: sac, speculative: r.speculative });
  }
  return pool;
}

/**
 * 후보 중 하나를 사람처럼 고른다.
 *  - 기본: loss 에 대한 소프트맥스(온도 T 는 humanness 가 커질수록 높아 실수가 늘어난다)
 *  - 손해가 큰 희생(speculative)은 기본 후보에서 빼고, boldness 확률로 "희생을 해 보고 싶은 기분"일 때만 후보가 된다
 *  - 이길 수 있는 짧은 메이트는 놓치지 않는다
 */
export function chooseMove(pool: PoolEntry[], params: HumanParams, rng: Rng): PoolEntry | null {
  if (pool.length === 0) return null;
  const h = clamp01(params.humanness), b = clamp01(params.boldness);

  const best = pool.reduce((a, c) => (c.score > a.score ? c : a));
  if (best.score >= 100000 - FORCED_MATE_IN * 100) return best;

  // 온도(센티폰): 0 → 3(거의 항상 최선), 0.4 → 약 35, 0.7 → 약 100, 1 → 200. 제곱이라 낮은 쪽을 세밀하게 조절한다.
  const T = 3 + 197 * h * h;
  const isBold = (e: PoolEntry) => e.sacrifice !== null && e.loss > SOUND_LOSS;
  const ordinary = pool.filter((e) => !isBold(e));
  const bold = pool.filter(isBold);

  // 희생을 해 보고 싶은 기분: 대담한 후보가 있을 때만, b 에 비례하는 확률
  if (bold.length > 0 && rng() < 0.4 * b) {
    const Ts = 100 + 200 * h; // 희생을 고를 때도 덜 나쁜 쪽을 더 선호한다
    return weighted(bold, (e) => Math.exp(-e.loss / Ts), rng);
  }
  const base = ordinary.length > 0 ? ordinary : pool;
  const minLoss = Math.min(...base.map((e) => e.loss));
  return weighted(base, (e) => Math.exp(-(e.loss - minLoss) / T), rng);
}

function weighted<T>(items: T[], weight: (t: T) => number, rng: Rng): T {
  const ws = items.map(weight);
  const total = ws.reduce((a, c) => a + c, 0);
  let r = rng() * total;
  for (let i = 0; i < items.length; i++) {
    r -= ws[i];
    if (r <= 0) return items[i];
  }
  return items[items.length - 1];
}

const clamp01 = (x: number) => Math.min(1, Math.max(0, x));

export type MoveTag = "brilliant" | "gambit" | "speculative" | "blunder" | "mistake" | "normal";

export interface MoveReport {
  tag: MoveTag;
  symbol: string;
  text: string;
  loss: number;
  sacrifice: SacrificeInfo | null;
}

/** 둔 수의 성격: 희생이면서 손해가 작으면 탁월(!!)/갬빗(!), 손해가 크면 블런더(??). */
export function classify(e: PoolEntry): MoveReport {
  const base = { loss: e.loss, sacrifice: e.sacrifice };
  if (e.sacrifice) {
    const what = KIND_TEXT[e.sacrifice.kind];
    if (e.loss <= SOUND_LOSS) {
      return e.sacrifice.kind === "pawn"
        ? { ...base, tag: "gambit", symbol: "!", text: `정확한 갬빗 — ${what}` }
        : { ...base, tag: "brilliant", symbol: "!!", text: `탁월한 희생 — ${what}` };
    }
    if (e.loss <= 150) return { ...base, tag: "speculative", symbol: "?!", text: `도박성 희생 — ${what}` };
    return { ...base, tag: "blunder", symbol: "??", text: `블런더 — ${what}` };
  }
  if (e.loss > 150) return { ...base, tag: "mistake", symbol: "?", text: "실수" };
  return { ...base, tag: "normal", symbol: "", text: "" };
}
