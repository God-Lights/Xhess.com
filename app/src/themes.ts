// 전략 테마: Albatross 평가 함수가 점수를 줄 때 "어느 칸의 어떤 특징이 몇 점이었는가"를 기록한 것(UCI themes 명령)을
// 보드 표시와 추천 전략 문장으로 바꾼다. DOM·엔진에 의존하지 않는 순수 로직이다.
//
// 용어: 각 테마는 한 쪽(owner)의 기물에 대한 것이고, cp 는 그 쪽 관점의 값이다(+ = 그 쪽의 강점, − = 그 쪽의 약점).
// "나" 관점으로 바꾸면 네 가지 역할이 된다.
//   내 강점(asset)   내 쪽, cp > 0   → 활용한다
//   내 약점(liability) 내 쪽, cp < 0   → 보완한다
//   상대 약점(opportunity) 상대 쪽, cp < 0 → 공략한다
//   상대 강점(threat) 상대 쪽, cp > 0   → 경계하거나 없앤다

import type { DrawShape } from "chessground/draw";

export type ThemeId =
  | "passed" | "unstoppable" | "doubled" | "isolated" | "outpost" | "rook_open" | "rook_semi"
  | "rook_seventh" | "bishop_pair" | "king_shield" | "king_danger" | "inactive" | "mopup";

export type Side = "white" | "black";

export interface ThemeEntry {
  id: ThemeId;
  color: Side;
  squares: string[];
  /** owner 관점의 값(센티폰, 게임 단계로 보간된 값) */
  cp: number;
  mg: number;
  eg: number;
}

export interface ThemeReport {
  phase: number;
  scale: number;
  /** 백 관점 전체 평가(센티폰) */
  white: number;
  themes: ThemeEntry[];
}

const IDS: readonly ThemeId[] = [
  "passed", "unstoppable", "doubled", "isolated", "outpost", "rook_open", "rook_semi", "rook_seventh",
  "bishop_pair", "king_shield", "king_danger", "inactive", "mopup",
];

/** 엔진의 `themes` 출력 줄들을 해석한다. 종료 줄("themes done ...")이 없으면 null. */
export function parseThemes(lines: readonly string[]): ThemeReport | null {
  const themes: ThemeEntry[] = [];
  let done: ThemeReport | null = null;
  for (const raw of lines) {
    const line = raw.trim();
    const t = /^theme (\S+) (white|black) (\S+) cp (-?\d+) mg (-?\d+) eg (-?\d+)$/.exec(line);
    if (t) {
      if (!IDS.includes(t[1] as ThemeId)) continue; // 모르는 테마는 건너뛴다(엔진이 새 테마를 추가해도 앱이 깨지지 않게)
      themes.push({
        id: t[1] as ThemeId,
        color: t[2] as Side,
        squares: t[3] === "-" ? [] : t[3].split(","),
        cp: Number(t[4]), mg: Number(t[5]), eg: Number(t[6]),
      });
      continue;
    }
    const d = /^themes done phase (\d+) scale (\d+) white (-?\d+)$/.exec(line);
    if (d) done = { phase: Number(d[1]), scale: Number(d[2]), white: Number(d[3]), themes: [] };
  }
  return done ? { ...done, themes } : null;
}

interface ThemeInfo {
  name: string;
  /** 보드 표시 색 */
  color: string;
  /** 원 안의 한 글자 라벨 */
  glyph: string;
}

export const THEME_INFO: Record<ThemeId, ThemeInfo> = {
  passed: { name: "통과폰", color: "#26c6da", glyph: "통" },
  unstoppable: { name: "막을 수 없는 통과폰", color: "#00e5ff", glyph: "★" },
  doubled: { name: "겹폰", color: "#ff7043", glyph: "겹" },
  isolated: { name: "고립폰", color: "#ef5350", glyph: "고" },
  outpost: { name: "나이트 전초기지", color: "#ab47bc", glyph: "전" },
  rook_open: { name: "열린 파일의 룩", color: "#ffa726", glyph: "룩" },
  rook_semi: { name: "반열린 파일의 룩", color: "#ffcc80", glyph: "룩" },
  rook_seventh: { name: "7랭크의 룩", color: "#fb8c00", glyph: "7" },
  bishop_pair: { name: "비숍 쌍", color: "#66bb6a", glyph: "쌍" },
  king_shield: { name: "킹 앞의 구멍", color: "#ec407a", glyph: "구" },
  king_danger: { name: "킹 위협", color: "#d81b60", glyph: "위" },
  inactive: { name: "갇힌 기물", color: "#8d6e63", glyph: "갇" },
  mopup: { name: "외로운 킹 몰이", color: "#5c6bc0", glyph: "몰" },
};

/** 보드 도형에 쓰는 브러시 이름 */
export const brushOf = (id: ThemeId) => `th-${id}`;

/** chessground drawable.brushes 에 추가할 테마 브러시들 */
export function themeBrushes(): Record<string, { key: string; color: string; opacity: number; lineWidth: number }> {
  const out: Record<string, { key: string; color: string; opacity: number; lineWidth: number }> = {};
  for (const id of IDS) out[brushOf(id)] = { key: `t${IDS.indexOf(id)}`, color: THEME_INFO[id].color, opacity: 0.85, lineWidth: 7 };
  return out;
}

export type Role = "asset" | "liability" | "opportunity" | "threat";

export interface RankedTheme extends ThemeEntry {
  mine: boolean;
  role: Role;
  /** 나에게 이로운 정도(+) / 해로운 정도(−), 센티폰 */
  net: number;
}

const ROLE_TEXT: Record<Role, string> = { asset: "내 강점", liability: "내 약점", opportunity: "상대 약점", threat: "상대 강점" };
export const roleText = (r: Role) => ROLE_TEXT[r];

export function roleOf(mine: boolean, cp: number): Role {
  if (mine) return cp >= 0 ? "asset" : "liability";
  return cp <= 0 ? "opportunity" : "threat";
}

/** 나(me) 관점으로 역할을 붙이고 영향이 큰 순서로 정렬한다. 값이 0 인 테마도 칸 표시를 위해 남긴다. */
export function rankThemes(report: ThemeReport, me: Side): RankedTheme[] {
  return report.themes
    .map((t): RankedTheme => {
      const mine = t.color === me;
      return { ...t, mine, role: roleOf(mine, t.cp), net: mine ? t.cp : -t.cp };
    })
    .sort((a, b) => Math.abs(b.cp) - Math.abs(a.cp) || IDS.indexOf(a.id) - IDS.indexOf(b.id));
}

/** "e5", "e5·c6" */
const sq = (t: ThemeEntry, max = 3) => t.squares.slice(0, max).join("·") + (t.squares.length > max ? " 외" : "");

type Advice = Partial<Record<Role, (t: ThemeEntry) => string>>;

/** 테마별·역할별 한 줄 조언. 정의되지 않은 조합(예: 겹폰이 내 강점)은 조언이 없다. */
const ADVICE: Record<ThemeId, Advice> = {
  passed: {
    asset: (t) => `통과폰 ${sq(t)}을(를) 전진시키고 룩·킹으로 뒷받침하세요`,
    threat: (t) => `상대 통과폰 ${sq(t)}을(를) 기물로 막고 교환을 피하세요`,
  },
  unstoppable: {
    asset: (t) => `${sq(t)}의 통과폰은 막을 수 없습니다 — 그대로 승급을 노리세요`,
    threat: (t) => `상대 통과폰 ${sq(t)}이(가) 막을 수 없는 상태입니다 — 킹을 보내거나 기물로 막으세요`,
  },
  doubled: {
    liability: (t) => `겹폰 ${sq(t)}: 폰 교환으로 구조를 정리하고 약점이 되지 않게 지키세요`,
    opportunity: (t) => `상대의 겹폰 ${sq(t)}은(는) 약점입니다 — 공격 목표로 삼으세요`,
  },
  isolated: {
    liability: (t) => `고립폰 ${sq(t)}: 기물로 지키고 불필요한 교환은 피하세요`,
    opportunity: (t) => `상대 고립폰 ${sq(t)}을(를) 룩·퀸으로 겨냥해 압박하세요`,
  },
  outpost: {
    asset: (t) => `나이트를 ${sq(t)} 전초기지에 계속 두세요`,
    threat: (t) => `상대 전초기지 ${sq(t)}의 나이트를 교환으로 없애세요`,
  },
  rook_open: {
    asset: (t) => `${sq(t)}의 룩으로 열린 파일을 장악하고 7랭크 침투를 노리세요`,
    threat: (t) => `상대 룩의 열린 파일(${sq(t)})을 막거나 룩을 교환하세요`,
  },
  rook_semi: {
    asset: (t) => `반열린 파일의 룩 ${sq(t)}으로 상대 폰을 압박하세요`,
    threat: (t) => `상대 룩이 반열린 파일(${sq(t)})을 쓰고 있습니다 — 그 파일의 폰을 지키세요`,
  },
  rook_seventh: {
    asset: (t) => `7랭크의 룩 ${sq(t)}으로 폰을 공격하고 킹을 가두세요`,
    threat: (t) => `상대 룩이 7랭크(${sq(t)})에 있습니다 — 쫓아내거나 룩을 교환하세요`,
  },
  bishop_pair: {
    asset: () => `비숍 쌍을 유지하세요 — 포지션을 열수록 유리합니다`,
    threat: () => `상대의 비숍 쌍 중 하나를 교환으로 없애세요`,
  },
  king_shield: {
    liability: (t) => `내 킹 앞 구멍(${sq(t)}): 폰으로 메우거나 킹을 안전한 곳으로 옮기세요`,
    opportunity: (t) => `상대 킹 앞 구멍(${sq(t)}): 그쪽으로 기물을 모아 공격하세요`,
  },
  king_danger: {
    liability: (t) => `내 킹 주변을 ${sq(t)}의 기물이 위협합니다 — 수비를 보강하거나 교환하세요`,
    opportunity: (t) => `상대 킹을 ${sq(t)}의 기물이 압박하고 있습니다 — 공격을 이어 가세요`,
  },
  inactive: {
    liability: (t) => `${sq(t)}의 기물이 갇혔습니다 — 활동할 칸을 만들어 주세요`,
    opportunity: (t) => `상대 ${sq(t)}의 기물이 갇혀 있습니다 — 계속 가두세요`,
  },
  mopup: {
    asset: (t) => `상대 킹 ${sq(t)}을(를) 가장자리로 몰아 체크메이트를 노리세요`,
  },
};

export interface PlanItem {
  theme: RankedTheme;
  text: string;
}

/** 추천 전략: 영향이 큰 테마부터 조언이 있는 것만 골라 최대 max 줄. 영향이 너무 작은(8cp 미만, 0 은 칸 표시용) 것은 제외. */
export function planFor(report: ThemeReport, me: Side, max = 4): PlanItem[] {
  const out: PlanItem[] = [];
  for (const t of rankThemes(report, me)) {
    if (Math.abs(t.cp) < 8) continue;
    const text = ADVICE[t.id][t.role]?.(t);
    if (text) out.push({ theme: t, text });
    if (out.length >= max) break;
  }
  return out;
}

/** "+0.53" / "-0.16" (센티폰 → 폰 단위) */
export const formatCp = (cp: number) => `${cp >= 0 ? "+" : "-"}${(Math.abs(cp) / 100).toFixed(2)}`;

/** 칩에 쓰는 한 줄: "내 통과폰 e5 +0.53" (값은 나에게 이로우면 +) */
export function chipLabel(t: RankedTheme): { title: string; value: string } {
  return { title: `${t.mine ? "내" : "상대"} ${THEME_INFO[t.id].name} ${sq(t)}`, value: formatCp(t.net) };
}

/** 칩/표시 켜고 끄기의 키 (같은 테마라도 내 것과 상대 것을 따로 켜고 끈다) */
export const themeKey = (t: Pick<RankedTheme, "id" | "mine">) => `${t.id}:${t.mine ? "me" : "opp"}`;

/** 보드에 그릴 도형: 켜진 테마의 칸마다 색 원과 한 글자 라벨. 영향이 큰 테마부터(겹치면 작은 것이 위에 오지 않게). */
export function themeShapes(ranked: readonly RankedTheme[], disabled: ReadonlySet<string>): DrawShape[] {
  const shapes: DrawShape[] = [];
  const seen = new Set<string>();
  for (const t of [...ranked].reverse()) {
    if (disabled.has(themeKey(t))) continue;
    for (const s of t.squares) {
      const k = `${t.id}@${s}`;
      if (seen.has(k)) continue;
      seen.add(k);
      shapes.push({ orig: s as DrawShape["orig"], brush: brushOf(t.id), label: { text: THEME_INFO[t.id].glyph } });
    }
  }
  return shapes;
}
