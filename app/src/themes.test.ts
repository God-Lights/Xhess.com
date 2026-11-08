import { describe, expect, it } from "vitest";
import {
  THEME_INFO, brushOf, chipLabel, formatCp, parseThemes, planFor, rankThemes, roleOf, themeBrushes, themeKey,
  themeShapes, type ThemeReport,
} from "./themes";

const LINES = [
  "theme passed white e5 cp 53 mg 35 eg 70",
  "theme isolated black a7,c7 cp -26 mg -24 eg -32",
  "theme doubled black c7 cp -13 mg -8 eg -18",
  "theme king_shield white g2 cp -22 mg -22 eg 0",
  "theme outpost black d4 cp 17 mg 22 eg 12",
  "theme bishop_pair white c1,f1 cp 40 mg 30 eg 50",
  "theme mopup white a8 cp 0 mg 0 eg 0",
  "themes done phase 14 scale 128 white 31",
];

const report = (): ThemeReport => parseThemes(LINES)!;

describe("parseThemes", () => {
  it("테마 줄과 종료 줄을 해석한다", () => {
    const r = report();
    expect(r.phase).toBe(14);
    expect(r.scale).toBe(128);
    expect(r.white).toBe(31);
    expect(r.themes).toHaveLength(7);
    expect(r.themes[0]).toEqual({ id: "passed", color: "white", squares: ["e5"], cp: 53, mg: 35, eg: 70 });
    expect(r.themes[1].squares).toEqual(["a7", "c7"]);
  });

  it("종료 줄이 없으면 null, 모르는 테마·잡음은 무시", () => {
    expect(parseThemes(LINES.slice(0, -1))).toBeNull();
    expect(parseThemes([])).toBeNull();
    const r = parseThemes(["info string hello", "theme future_theme white e4 cp 5 mg 5 eg 5", "garbage", "themes done phase 0 scale 128 white 0"]);
    expect(r?.themes).toEqual([]);
  });

  it("칸이 없는 테마(-)", () => {
    const r = parseThemes(["theme passed white - cp 1 mg 1 eg 1", "themes done phase 0 scale 128 white 0"]);
    expect(r?.themes[0].squares).toEqual([]);
  });
});

describe("역할 분류 (나 관점)", () => {
  it("내 쪽: 값 ≥ 0 이면 강점, < 0 이면 약점 / 상대 쪽: 값 ≤ 0 이면 상대 약점(공략), > 0 이면 상대 강점(경계)", () => {
    expect(roleOf(true, 10)).toBe("asset");
    expect(roleOf(true, -10)).toBe("liability");
    expect(roleOf(false, -10)).toBe("opportunity");
    expect(roleOf(false, 10)).toBe("threat");
  });

  it("영향이 큰 순서로 정렬하고 net 은 나에게 이로우면 +", () => {
    const ranked = rankThemes(report(), "white");
    expect(ranked.map((t) => t.id)).toEqual(["passed", "bishop_pair", "isolated", "king_shield", "outpost", "doubled", "mopup"]);
    const byId = Object.fromEntries(ranked.map((t) => [t.id, t]));
    expect(byId.passed).toMatchObject({ mine: true, role: "asset", net: 53 });
    expect(byId.isolated).toMatchObject({ mine: false, role: "opportunity", net: 26 });   // 흑의 고립폰 = 백에게 공략 대상
    expect(byId.outpost).toMatchObject({ mine: false, role: "threat", net: -17 });
    expect(byId.king_shield).toMatchObject({ mine: true, role: "liability", net: -22 });
    // 흑으로 보면 반대
    const black = Object.fromEntries(rankThemes(report(), "black").map((t) => [t.id, t]));
    expect(black.passed).toMatchObject({ mine: false, role: "threat", net: -53 });
    expect(black.isolated).toMatchObject({ mine: true, role: "liability", net: -26 });
  });
});

describe("추천 전략 문장", () => {
  it("영향이 큰 것부터 역할에 맞는 조언을 만든다", () => {
    const plan = planFor(report(), "white", 4);
    expect(plan).toHaveLength(4);
    expect(plan[0].text).toBe("통과폰 e5을(를) 전진시키고 룩·킹으로 뒷받침하세요");
    expect(plan[1].text).toBe("비숍 쌍을 유지하세요 — 포지션을 열수록 유리합니다");
    expect(plan[2].text).toBe("상대 고립폰 a7·c7을(를) 룩·퀸으로 겨냥해 압박하세요");
    expect(plan[3].text).toBe("내 킹 앞 구멍(g2): 폰으로 메우거나 킹을 안전한 곳으로 옮기세요");
  });

  it("같은 포지션을 흑 관점으로 보면 조언이 바뀐다", () => {
    const texts = planFor(report(), "black", 6).map((p) => p.text);
    expect(texts).toContain("상대 통과폰 e5을(를) 기물로 막고 교환을 피하세요");
    expect(texts).toContain("고립폰 a7·c7: 기물로 지키고 불필요한 교환은 피하세요");
    expect(texts).toContain("상대의 비숍 쌍 중 하나를 교환으로 없애세요");
  });

  it("영향이 작은(8cp 미만) 테마와 조언이 없는 조합은 제외하고 max 를 지킨다", () => {
    const texts = planFor(report(), "white", 10).map((p) => p.text);
    expect(texts.some((t) => t.includes("몰아"))).toBe(false);   // mopup cp 0
    expect(planFor(report(), "white", 2)).toHaveLength(2);
    // 겹폰이 "내 강점"인 경우는 없다 → 조언 없음
    const odd = parseThemes(["theme doubled white c2 cp 30 mg 30 eg 30", "themes done phase 0 scale 128 white 0"])!;
    expect(planFor(odd, "white")).toEqual([]);
  });

  it("칸이 많으면 3개까지만 쓰고 '외' 를 붙인다", () => {
    const many = parseThemes(["theme isolated black a7,b7,c7,d7,e7 cp -50 mg -50 eg -50", "themes done phase 0 scale 128 white 0"])!;
    expect(planFor(many, "white")[0].text).toBe("상대 고립폰 a7·b7·c7 외을(를) 룩·퀸으로 겨냥해 압박하세요");
  });
});

describe("표시용 문자열", () => {
  it("formatCp", () => {
    expect(formatCp(53)).toBe("+0.53");
    expect(formatCp(-16)).toBe("-0.16");
    expect(formatCp(0)).toBe("+0.00");
  });

  it("chipLabel: 내/상대와 나 관점의 값", () => {
    const [passed, , isolated] = rankThemes(report(), "white");
    expect(chipLabel(passed)).toEqual({ title: "내 통과폰 e5", value: "+0.53" });
    expect(chipLabel(isolated)).toEqual({ title: "상대 고립폰 a7·c7", value: "+0.26" });
  });

  it("themeKey 는 같은 테마라도 내 것과 상대 것을 구분한다", () => {
    expect(themeKey({ id: "passed", mine: true })).toBe("passed:me");
    expect(themeKey({ id: "passed", mine: false })).toBe("passed:opp");
  });
});

describe("보드 도형", () => {
  it("켜진 테마의 칸마다 색 원과 한 글자 라벨", () => {
    const shapes = themeShapes(rankThemes(report(), "white"), new Set());
    const squares = shapes.map((s) => `${s.orig}:${s.brush}`);
    expect(squares).toContain("e5:th-passed");
    expect(squares).toContain("a7:th-isolated");
    expect(squares).toContain("c7:th-isolated");
    expect(squares).toContain("c7:th-doubled");   // 한 칸이 두 테마에 걸리면 둘 다 그린다
    expect(shapes.find((s) => s.orig === "e5")?.label?.text).toBe("통");
    expect(shapes.every((s) => !s.dest)).toBe(true);   // 원(칸 표시)이지 화살표가 아니다
  });

  it("꺼진 테마는 그리지 않는다 (내 것과 상대 것을 따로)", () => {
    const ranked = rankThemes(report(), "white");
    const off = themeShapes(ranked, new Set(["isolated:opp"]));
    expect(off.some((s) => s.brush === "th-isolated")).toBe(false);
    expect(off.some((s) => s.brush === "th-passed")).toBe(true);
    expect(themeShapes(ranked, new Set(ranked.map(themeKey)))).toEqual([]);
    // 내 고립폰은 켜져 있어도 흑 입장에서는 "내 것"이다
    const black = rankThemes(report(), "black");
    expect(themeShapes(black, new Set(["isolated:opp"])).some((s) => s.brush === "th-isolated")).toBe(true);
  });

  it("브러시는 모든 테마에 대해 있고 서로 다른 key 를 가진다", () => {
    const brushes = themeBrushes();
    const ids = Object.keys(THEME_INFO) as (keyof typeof THEME_INFO)[];
    for (const id of ids) expect(brushes[brushOf(id)]).toMatchObject({ color: THEME_INFO[id].color });
    expect(new Set(Object.values(brushes).map((b) => b.key)).size).toBe(ids.length);
  });
});
