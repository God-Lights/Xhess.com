// UCI 프로토콜 계층. UI 는 Engine 인터페이스만 알고, 실제 엔진이
// Stockfish(WASM)인지 Albatross(네이티브/WASM)인지는 Transport 가 결정한다.

export interface Transport {
  send(cmd: string): void;
  onLine(cb: (line: string) => void): void;
  terminate(): void;
}

export interface SearchInfo {
  depth: number;
  /** 엔진(수를 두는 쪽) 관점의 centipawn */
  cp?: number;
  /** 엔진 관점의 메이트까지 수 (음수면 당하는 쪽) */
  mate?: number;
  pv: string[];
}

export interface GoOptions {
  movetime?: number;
  depth?: number;
}

export function parseInfo(line: string): SearchInfo | null {
  if (!line.startsWith("info ") || !line.includes(" score ")) return null;
  const t = line.split(/\s+/);
  const out: SearchInfo = { depth: 0, pv: [] };
  for (let i = 1; i < t.length; i++) {
    if (t[i] === "depth") out.depth = Number(t[++i]);
    else if (t[i] === "score") {
      const kind = t[++i];
      const val = Number(t[++i]);
      if (kind === "cp") out.cp = val;
      else if (kind === "mate") out.mate = val;
    } else if (t[i] === "pv") {
      out.pv = t.slice(i + 1);
      break;
    }
  }
  return out;
}

export function parseBestMove(line: string): string | null {
  const m = /^bestmove\s+(\S+)/.exec(line);
  if (!m || m[1] === "(none)" || m[1] === "0000") return null;
  return m[1];
}

export class UciEngine {
  private waiters: Array<(line: string) => boolean> = [];
  private infoCb: ((i: SearchInfo) => void) | null = null;

  constructor(private t: Transport) {
    t.onLine((line) => {
      this.waiters = this.waiters.filter((w) => !w(line));
      const info = parseInfo(line);
      if (info) this.infoCb?.(info);
    });
  }

  private waitFor(pred: (l: string) => boolean): Promise<string> {
    return new Promise((res) => {
      this.waiters.push((l) => (pred(l) ? (res(l), true) : false));
    });
  }

  async init(): Promise<void> {
    const ok = this.waitFor((l) => l === "uciok");
    this.t.send("uci");
    await ok;
    await this.ready();
  }

  async ready(): Promise<void> {
    const ok = this.waitFor((l) => l === "readyok");
    this.t.send("isready");
    await ok;
  }

  setOption(name: string, value: string | number): void {
    this.t.send(`setoption name ${name} value ${value}`);
  }

  async newGame(): Promise<void> {
    this.t.send("ucinewgame");
    await this.ready();
  }

  /** startFen + 지금까지의 UCI 수 목록으로 탐색해 최선수(UCI 표기)를 돌려준다. */
  async bestMove(
    startFen: string | null,
    moves: string[],
    opt: GoOptions,
    onInfo?: (i: SearchInfo) => void,
  ): Promise<string | null> {
    this.infoCb = onInfo ?? null;
    const base = startFen ? `fen ${startFen}` : "startpos";
    this.t.send(`position ${base}${moves.length ? " moves " + moves.join(" ") : ""}`);
    const done = this.waitFor((l) => l.startsWith("bestmove"));
    const go = opt.depth ? `go depth ${opt.depth}` : `go movetime ${opt.movetime ?? 1000}`;
    this.t.send(go);
    const line = await done;
    this.infoCb = null;
    return parseBestMove(line);
  }

  stop(): void {
    this.t.send("stop");
  }

  quit(): void {
    this.t.send("quit");
    this.t.terminate();
  }
}
