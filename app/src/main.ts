import { Chessground } from "chessground";
import type { Api } from "chessground/api";
import "chessground/assets/chessground.base.css";
import "chessground/assets/chessground.brown.css";
import "chessground/assets/chessground.cburnett.css";
import "./style.css";
import { Game, type Color, type Key } from "./game";
import { UciEngine, type SearchInfo } from "./engine/uci";
import { ALBATROSS_URL, createWorkerTransport, STOCKFISH_URL } from "./engine/workerTransport";
import { createTauriTransport, isTauri } from "./engine/tauriTransport";
import type { DrawShape } from "chessground/draw";
import { Analyzer } from "./analysis";
import { buildPlanShapes, buildShapes, describeLines, planFromShapes, type PlanReport } from "./strategy";
import { DEFAULT_PARAMS, type HumanParams } from "./humanmode";
import { pickHumanMove, type HumanChoice } from "./humanplay";

const $ = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;

const game = new Game();
let human: Color = "white";
let engineBusy = false;
let engineReady = false;
let epoch = 0; // 새 게임/무르기 시 진행 중이던 엔진 응답을 무시하기 위한 세대 번호

let engine: UciEngine | null = null;
const PATH_KEY = "xhess.enginePath";
const KIND_KEY = "xhess.engineKind";
type EngineKind = "stockfish" | "albatross";
let engineKind: EngineKind = (() => { try { return localStorage.getItem(KIND_KEY) === "albatross" ? "albatross" : "stockfish"; } catch { return "stockfish"; } })();

const ground: Api = Chessground($("board"), {
  fen: game.fen,
  coordinates: true,
  highlight: { lastMove: true, check: true },
  animation: { duration: 180 },
  movable: { free: false, showDests: true, events: { after: (o, d) => onUserMove(o as Key, d as Key) } },
  draggable: { showGhost: true },
  // 오른쪽 클릭 드래그로 그린 화살표 = "내 계획"
  drawable: { enabled: true, onChange: (shapes) => onUserShapes(shapes) },
});

// ───────── 전략 마킹: 엔진 추천 화살표와 내 계획 평가 ─────────
const MARKS_KEY = "xhess.marks";
let marksOn = (() => { try { return localStorage.getItem(MARKS_KEY) !== "off"; } catch { return true; } })();
let analysisFen: string | null = null;   // 지금 분석 중이거나 분석을 마친 국면
let planActive = false;
let planTimer: ReturnType<typeof setTimeout> | undefined;

const analyzer = new Analyzer(
  () => engine,
  {
    onLines(lines, fen) {
      if (fen !== game.fen || !marksOn) return;
      if (!planActive) ground.setAutoShapes(buildShapes(fen, lines));
      renderCandidates(fen, lines);
    },
    onPlan(report, planInfo, bestInfo, fen) {
      if (fen !== game.fen || !marksOn) return;
      planActive = true;
      ground.setAutoShapes(buildPlanShapes(fen, planInfo, bestInfo));
      renderPlan(report);
    },
  },
  { skillRestore: () => Number((document.getElementById("level") as HTMLInputElement).value) },
);

// ───────── 인간 모드: 희생(탁월 또는 블런더)과 실수를 섞어 두는 엔진 ─────────
const HUMAN_KEY = "xhess.human";
type EngineMode = "best" | "human";
let engineMode: EngineMode = "best";
const humanParams: HumanParams = { ...DEFAULT_PARAMS };
try {
  const saved = JSON.parse(localStorage.getItem(HUMAN_KEY) ?? "null");
  if (saved) {
    if (saved.mode === "human") engineMode = "human";
    if (typeof saved.humanness === "number") humanParams.humanness = saved.humanness;
    if (typeof saved.boldness === "number") humanParams.boldness = saved.boldness;
  }
} catch { /* 저장 불가·손상된 값은 기본값 사용 */ }

function saveHuman(): void {
  try { localStorage.setItem(HUMAN_KEY, JSON.stringify({ mode: engineMode, ...humanParams })); } catch { /* 무시 */ }
}

function showHumanNote(c: HumanChoice | null): void {
  const box = $("humanNote");
  box.replaceChildren();
  if (!c || c.report.tag === "normal") { box.hidden = true; return; }
  const r = c.report;
  box.className = `human-note t-${r.tag}`;
  const head = document.createElement("div");
  const b = document.createElement("b");
  b.textContent = `${c.entry.san}${r.symbol}`;
  head.append(b, ` — ${r.text}`);
  const sub = document.createElement("div");
  sub.className = "sub";
  const lossText = r.loss > 0 ? `엔진 최선(${c.best.san})보다 ${(r.loss / 100).toFixed(2)} 불리` : "엔진 최선과 같은 평가";
  sub.textContent = r.sacrifice ? `${lossText} · 물질 ${(r.sacrifice.swing / 100).toFixed(1)}` : lossText;
  box.append(head, sub);
  box.hidden = false;
}

function applyEngineModeUi(): void {
  $<HTMLSelectElement>("engineMode").value = engineMode;
  $("humanOpts").hidden = engineMode !== "human";
  // 인간 모드는 난이도 슬라이더 대신 실수 빈도를 쓰고, Albatross 에는 난이도 옵션이 없다.
  $<HTMLInputElement>("level").disabled = engineMode === "human" || engineKind === "albatross";
  $<HTMLInputElement>("humanness").value = String(Math.round(humanParams.humanness * 100));
  $<HTMLInputElement>("boldness").value = String(Math.round(humanParams.boldness * 100));
  $("humanOut").textContent = String(Math.round(humanParams.humanness * 100));
  $("boldOut").textContent = String(Math.round(humanParams.boldness * 100));
  if (engineMode !== "human") showHumanNote(null);
}

function renderCandidates(fen: string, lines: import("./engine/uci").SearchInfo[]): void {
  const ol = $("cands");
  ol.replaceChildren();
  for (const c of describeLines(fen, lines)) {
    const li = document.createElement("li");
    const san = document.createElement("b"); san.textContent = c.san;
    const sc = document.createElement("span"); sc.className = "score"; sc.textContent = c.score;
    const cont = document.createElement("span"); cont.className = "cont"; cont.textContent = c.continuation.join(" ");
    li.append(san, sc, cont);
    ol.append(li);
  }
}

function renderPlan(r: PlanReport): void {
  const box = $("planBox");
  box.replaceChildren();
  const line = (cls: string, text: string) => { const d = document.createElement("div"); d.className = cls; d.textContent = text; return d; };
  box.append(line(`v-${r.verdict}`, `내 계획 ${r.san} ${r.score} — ${r.verdictText}`));
  box.append(line("sub", `엔진 최선 ${r.bestSan} ${r.bestScore}${r.loss > 0 ? ` (손해 ${(r.loss / 100).toFixed(2)})` : ""}`));
  if (r.expectedReplySan) {
    const match = r.replyMatches === undefined ? "" : r.replyMatches ? " · 내 예상과 일치" : " · 내 예상과 다름";
    box.append(line("sub", `예상 응수 ${r.expectedReplySan}${match}${r.continuation.length > 1 ? ` → ${r.continuation.slice(1).join(" ")}` : ""}`));
  }
  box.hidden = false;
}

function clearMarksView(): void {
  planActive = false;
  clearTimeout(planTimer);
  ground.setAutoShapes([]);
  ground.setShapes([]);
  $("cands").replaceChildren();
  $("planBox").hidden = true;
}

/** 지금 분석해도 되는 상태인가: 내 차례, 엔진이 쉬는 중, 대국이 끝나지 않음 */
function canAnalyze(): boolean {
  return marksOn && engineReady && !engineBusy && game.turn === human && !game.outcome().over;
}

function refreshAnalysis(): void {
  if (!canAnalyze()) {
    if (analysisFen !== null) { analysisFen = null; void analyzer.cancel(); clearMarksView(); }
    return;
  }
  if (analysisFen === game.fen) return;
  analysisFen = game.fen;
  clearMarksView();
  void analyzer.recommend(game.uciMoves, game.fen);
}

function onUserShapes(shapes: DrawShape[]): void {
  clearTimeout(planTimer);
  if (!canAnalyze()) return;
  const plan = planFromShapes(game.fen, shapes);
  if (plan.moves.length === 0) {
    if (planActive) { planActive = false; $("planBox").hidden = true; analysisFen = null; refreshAnalysis(); }
    return;
  }
  // 화살표를 여러 개 연달아 그릴 때마다 평가하지 않도록 잠시 기다린다.
  planTimer = setTimeout(() => void analyzer.evaluatePlan(game.uciMoves, game.fen, plan), 400);
}

$<HTMLInputElement>("marksOn").checked = marksOn;
$<HTMLInputElement>("marksOn").onchange = (e) => {
  marksOn = (e.target as HTMLInputElement).checked;
  try { localStorage.setItem(MARKS_KEY, marksOn ? "on" : "off"); } catch { /* 저장 불가 환경은 무시 */ }
  if (!marksOn) { analysisFen = null; void analyzer.cancel(); clearMarksView(); }
  else sync();
};

function setStatus(t: string) { $("status").textContent = t; }

function sync(): void {
  const mine = !engineBusy && game.turn === human && !game.outcome().over;
  ground.set({
    fen: game.fen,
    orientation: ground.state.orientation,
    turnColor: game.turn,
    check: game.inCheck,
    lastMove: game.lastMove,
    movable: { color: mine ? human : undefined, dests: mine ? (game.dests() as Map<never, never[]>) : new Map() },
  });
  renderMoves();
  const o = game.outcome();
  if (o.over) setStatus(o.text);
  else if (!engineReady) setStatus("엔진 로딩 중…");
  else if (engineBusy) setStatus("엔진이 생각 중…");
  else setStatus(game.turn === human ? "당신의 차례" : "");
  refreshAnalysis();
}

function renderMoves(): void {
  const ol = $("moves");
  ol.replaceChildren();
  const h = game.history;
  for (let i = 0; i < h.length; i += 2) {
    const n = document.createElement("li"); n.className = "n"; n.textContent = `${i / 2 + 1}.`;
    const a = document.createElement("li"); a.textContent = h[i].san;
    const b = document.createElement("li"); b.textContent = h[i + 1]?.san ?? "";
    ol.append(n, a, b);
  }
  ol.scrollTop = ol.scrollHeight;
}

function onUserMove(from: Key, to: Key): void {
  if (game.isPromotion(from, to)) {
    askPromotion(human).then((p) => { game.move(from, to, p); afterMove(); });
  } else {
    game.move(from, to);
    afterMove();
  }
}

function afterMove(): void {
  showHumanNote(null);
  sync();
  void engineTurn();
}

const PIECES: Record<string, string> = { q: "♛", r: "♜", b: "♝", n: "♞" };
function askPromotion(_c: Color): Promise<string> {
  return new Promise((res) => {
    const el = $("promo");
    const box = document.createElement("div"); box.className = "box";
    for (const [k, glyph] of Object.entries(PIECES)) {
      const b = document.createElement("button"); b.textContent = glyph;
      b.onclick = () => { el.hidden = true; res(k); };
      box.append(b);
    }
    el.replaceChildren(box);
    el.hidden = false;
  });
}

function showEval(i: SearchInfo, engineColor: Color): void {
  // 엔진 관점 점수를 백 관점으로 변환
  const sign = engineColor === "white" ? 1 : -1;
  let whiteCp: number, label: string;
  if (i.mate !== undefined) {
    whiteCp = sign * (i.mate > 0 ? 3000 : -3000);
    label = `M${sign * i.mate}`;
  } else {
    whiteCp = sign * (i.cp ?? 0);
    label = (whiteCp / 100).toFixed(2);
  }
  // 시그모이드로 0~100% 로 매핑
  const pct = 50 + 50 * (2 / (1 + Math.exp(-whiteCp / 400)) - 1);
  $("evalFill").style.height = `${pct}%`;
  $("info").textContent = `깊이 ${i.depth} · 평가 ${label}`;
}

async function engineTurn(): Promise<void> {
  if (!engineReady || engineBusy || game.outcome().over || game.turn === human) return;
  engineBusy = true;
  const my = epoch;
  sync();
  await analyzer.cancel(); // 분석이 완전히 멈춘 뒤에만 엔진이 두도록 한다
  if (my !== epoch) return;
  const engineColor = game.turn;
  let best: string | null;
  let humanChoice: HumanChoice | null = null;
  if (engineMode === "human") {
    humanChoice = await pickHumanMove(engine!, game.uciMoves, game.fen, humanParams, {
      skillRestore: () => Number((document.getElementById("level") as HTMLInputElement).value),
      cancelled: () => my !== epoch,
      onBest: (i) => { if (my === epoch) showEval(i, engineColor); },
    });
    if (my !== epoch) return;
    // 후보를 얻지 못했으면(예: 엔진 오류) 표준 탐색으로 대신 둔다
    best = humanChoice ? humanChoice.entry.uci : await engine!.bestMove(null, game.uciMoves, { movetime: 800 });
  } else {
    best = await engine!.bestMove(null, game.uciMoves, { movetime: 800 }, (i) => {
      if (my === epoch) showEval(i, engineColor);
    });
  }
  if (my !== epoch) return; // 그 사이 새 게임/무르기가 일어남
  engineBusy = false;
  if (best) game.moveUci(best);
  showHumanNote(humanChoice);
  sync();
}

async function resetEngineSearch(): Promise<void> {
  epoch++;
  analysisFen = null;
  await analyzer.cancel();
  analyzer.forget();
  if (engineBusy) { engine?.stop(); engineBusy = false; }
}

async function newGame(): Promise<void> {
  await resetEngineSearch();
  game.reset();
  human = $<HTMLSelectElement>("side").value as Color;
  ground.set({ orientation: human });
  $("info").textContent = "";
  showHumanNote(null);
  $("evalFill").style.height = "50%";
  if (engineReady) await engine?.newGame();
  sync();
  void engineTurn();
}

$("newGame").onclick = () => void newGame();
$("flip").onclick = () => ground.toggleOrientation();
$("undo").onclick = async () => {
  await resetEngineSearch();
  // 사람 차례로 돌아올 때까지 되돌린다 (엔진 수 + 내 수)
  if (game.turn !== human) game.undo();
  else { game.undo(); game.undo(); }
  if (game.turn !== human) game.undo();
  sync();
};
$("copyPgn").onclick = async () => {
  try { await navigator.clipboard.writeText(game.pgn); setStatus("PGN 을 복사했습니다"); }
  catch { setStatus("복사 실패 — 브라우저 권한을 확인하세요"); }
};
$<HTMLSelectElement>("side").onchange = () => void newGame();
$<HTMLInputElement>("level").oninput = (e) => {
  const v = (e.target as HTMLInputElement).value;
  $("lvlOut").textContent = v;
  if (engineReady) engine?.setOption("Skill Level", v);
};

function savedPath(): string {
  try { return localStorage.getItem(PATH_KEY) ?? ""; } catch { return ""; }
}
function savePath(v: string): void {
  try { localStorage.setItem(PATH_KEY, v); } catch { /* 저장 불가 환경은 무시 */ }
}
const baseName = (p: string) => p.split(/[\\/]/).pop() || p;

function withTimeout<T>(p: Promise<T>, ms: number): Promise<T> {
  return new Promise((res, rej) => {
    const t = setTimeout(() => rej(new Error("응답 없음")), ms);
    p.then((v) => { clearTimeout(t); res(v); }, (e) => { clearTimeout(t); rej(e); });
  });
}

/** 데스크톱이면 로컬(네이티브) 엔진을, 아니거나 실패하면 WASM 엔진을 시작한다. */
async function startEngine(): Promise<void> {
  await resetEngineSearch();
  engineReady = false;
  engine?.quit();
  engine = null;
  sync();

  let label = "";
  let native = false;
  if (engineKind === "albatross") {
    // 자체 엔진(WebAssembly). 다운로드·컴파일에 시간이 걸릴 수 있어 넉넉히 기다리고, 실패하면 Stockfish 로 대신한다.
    let candidate: UciEngine | null = null;
    try {
      candidate = new UciEngine(createWorkerTransport(ALBATROSS_URL));
      await withTimeout(candidate.init(), 20000);
      engine = candidate;
      label = "Albatross 0.4 (WASM, 자체 엔진)";
    } catch (e) {
      candidate?.quit();
      console.warn("albatross unavailable", e);
      label = `Stockfish WASM (Albatross 실패: ${e instanceof Error ? e.message : e})`;
    }
  } else if (isTauri()) {
    let candidate: UciEngine | null = null;
    try {
      const t = await createTauriTransport(savedPath());
      candidate = new UciEngine(t);
      const mine = candidate;
      t.onExit(() => {
        if (engine === mine) { engineReady = false; engineBusy = false; setStatus("엔진 프로세스가 종료되었습니다. '엔진 적용'으로 다시 시작하세요."); }
      });
      await withTimeout(candidate.init(), 10000);
      engine = candidate;
      native = true;
      label = /xhess-stockfish/i.test(t.path) ? "로컬 엔진 · Stockfish 18 (번들)" : `로컬 엔진 · ${baseName(t.path)}`;
    } catch (e) {
      candidate?.quit();
      console.warn("native engine unavailable", e);
      label = `WASM 엔진 (로컬 엔진 실패: ${e instanceof Error ? e.message : e})`;
    }
  }
  if (!engine) {
    engine = new UciEngine(createWorkerTransport(STOCKFISH_URL));
    await engine.init();
    if (!label) label = "WASM 엔진 (브라우저)";
  }

  if (native) {
    engine.setOption("Threads", Math.max(1, Math.min(8, (navigator.hardwareConcurrency || 2) - 1)));
    engine.setOption("Hash", 256);
  }
  if (engineKind === "albatross" && engine && label.startsWith("Albatross")) engine.setOption("Hash", 32);
  else engine.setOption("Skill Level", $<HTMLInputElement>("level").value);  // Albatross 에는 난이도 옵션이 없다
  await engine.ready();
  $("engineLabel").textContent = label;
  engineReady = true;
  sync();
  void engineTurn();
}

if (isTauri()) {
  $("enginePicker").hidden = false;
  $<HTMLInputElement>("enginePath").value = savedPath();
  $("applyEngine").onclick = () => {
    savePath($<HTMLInputElement>("enginePath").value.trim());
    void startEngine().catch((e) => setStatus(`엔진 오류: ${e}`));
  };
}

sync();
startEngine().catch((e) => setStatus(`엔진 오류: ${e}`));

$<HTMLSelectElement>("engineKind").value = engineKind;
$<HTMLSelectElement>("engineKind").onchange = (e) => {
  engineKind = (e.target as HTMLSelectElement).value as EngineKind;
  try { localStorage.setItem(KIND_KEY, engineKind); } catch { /* 저장 불가 환경은 무시 */ }
  applyEngineModeUi();
  void startEngine().catch((err) => setStatus(`엔진 오류: ${err}`));
};

$<HTMLSelectElement>("engineMode").onchange = (e) => {
  engineMode = (e.target as HTMLSelectElement).value as EngineMode;
  saveHuman();
  applyEngineModeUi();
};
$<HTMLInputElement>("humanness").oninput = (e) => {
  humanParams.humanness = Number((e.target as HTMLInputElement).value) / 100;
  $("humanOut").textContent = String(Math.round(humanParams.humanness * 100));
  saveHuman();
};
$<HTMLInputElement>("boldness").oninput = (e) => {
  humanParams.boldness = Number((e.target as HTMLInputElement).value) / 100;
  $("boldOut").textContent = String(Math.round(humanParams.boldness * 100));
  saveHuman();
};
applyEngineModeUi();

// 테스트/디버깅용
(window as unknown as { __xhess: unknown }).__xhess = {
  game,
  ground,
  // 테스트용: 주어진 수순의 국면에서 내 색을 정해 시작한다(엔진 차례면 바로 둔다)
  async setPosition(moves: string[], side: Color) {
    await resetEngineSearch();
    game.reset();
    for (const mv of moves) game.moveUci(mv);
    human = side;
    $<HTMLSelectElement>("side").value = side;
    ground.set({ orientation: side });
    showHumanNote(null);
    sync();
    void engineTurn();
  },
};

// PWA: 웹(모바일 포함)에서만 서비스 워커 등록. Tauri 데스크톱에서는 불필요.
if (import.meta.env.PROD && "serviceWorker" in navigator && !("__TAURI_INTERNALS__" in window)) {
  navigator.serviceWorker.register(`${import.meta.env.BASE_URL}sw.js`).catch(() => {});
}
