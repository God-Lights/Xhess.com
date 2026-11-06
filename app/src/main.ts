import { Chessground } from "chessground";
import type { Api } from "chessground/api";
import "chessground/assets/chessground.base.css";
import "chessground/assets/chessground.brown.css";
import "chessground/assets/chessground.cburnett.css";
import "./style.css";
import { Game, type Color, type Key } from "./game";
import { UciEngine, type SearchInfo } from "./engine/uci";
import { createWorkerTransport, STOCKFISH_URL } from "./engine/workerTransport";
import { createTauriTransport, isTauri } from "./engine/tauriTransport";

const $ = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;

const game = new Game();
let human: Color = "white";
let engineBusy = false;
let engineReady = false;
let epoch = 0; // 새 게임/무르기 시 진행 중이던 엔진 응답을 무시하기 위한 세대 번호

let engine: UciEngine | null = null;
const PATH_KEY = "xhess.enginePath";

const ground: Api = Chessground($("board"), {
  fen: game.fen,
  coordinates: true,
  highlight: { lastMove: true, check: true },
  animation: { duration: 180 },
  movable: { free: false, showDests: true, events: { after: (o, d) => onUserMove(o as Key, d as Key) } },
  draggable: { showGhost: true },
});

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
  const engineColor = game.turn;
  const best = await engine!.bestMove(null, game.uciMoves, { movetime: 800 }, (i) => {
    if (my === epoch) showEval(i, engineColor);
  });
  if (my !== epoch) return; // 그 사이 새 게임/무르기가 일어남
  engineBusy = false;
  if (best) game.moveUci(best);
  sync();
}

async function resetEngineSearch(): Promise<void> {
  epoch++;
  if (engineBusy) { engine?.stop(); engineBusy = false; }
}

async function newGame(): Promise<void> {
  await resetEngineSearch();
  game.reset();
  human = $<HTMLSelectElement>("side").value as Color;
  ground.set({ orientation: human });
  $("info").textContent = "";
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
  if (isTauri()) {
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
  engine.setOption("Skill Level", $<HTMLInputElement>("level").value);
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

// 테스트/디버깅용
(window as unknown as { __xhess: unknown }).__xhess = { game };

// PWA: 웹(모바일 포함)에서만 서비스 워커 등록. Tauri 데스크톱에서는 불필요.
if (import.meta.env.PROD && "serviceWorker" in navigator && !("__TAURI_INTERNALS__" in window)) {
  navigator.serviceWorker.register(`${import.meta.env.BASE_URL}sw.js`).catch(() => {});
}
