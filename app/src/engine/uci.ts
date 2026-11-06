// UCI 프로토콜 계층. UI 는 Engine 인터페이스만 알고, 실제 엔진이
// Stockfish(WASM)인지 Albatross(네이티브/WASM)인지는 Transport 가 결정한다.

export interface Transport {
  send(cmd: string): void;
  onLine(cb: (line: string) => void): void;
  terminate(): void;
}

export interface SearchInfo {
  /** MultiPV 라인 번호(1부터). MultiPV 를 쓰지 않는 엔진은 항상 1 */
  multipv: number;
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

export interface AnalysisOptions {
  /** 동시에 보고 싶은 후보 수(라인) 개수 */
  multiPv: number;
  /** 지정하면 이 수들만 평가한다(UCI searchmoves) */
  searchMoves?: string[];
  movetime: number;
  /** 분석 중에는 약화 설정(Skill Level)을 풀고 끝나면 restore 값으로 되돌린다 */
  skill?: { analysis: number; restore: number };
}

export interface AnalysisHandle {
  /** 탐색이 끝나고(옵션 복원까지) 엔진이 다시 쓸 수 있게 되면 이행된다 */
  done: Promise<void>;
  /** 탐색을 멈추고 done 을 기다린다 */
  stop(): Promise<void>;
}

export function parseInfo(line: string): SearchInfo | null {
  if (!line.startsWith("info ") || !line.includes(" score ")) return null;
  const t = line.split(/\s+/);
  const out: SearchInfo = { multipv: 1, depth: 0, pv: [] };
  for (let i = 1; i < t.length; i++) {
    if (t[i] === "depth") out.depth = Number(t[++i]);
    else if (t[i] === "multipv") out.multipv = Number(t[++i]);
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

  /**
   * 후보 수 여러 개(MultiPV)를 분석한다. 진행 중에는 onInfo 가 라인별로 계속 호출된다.
   * 대국용 bestMove() 와 동시에 쓸 수 없으므로, 끝났거나 stop() 으로 멈춘 뒤에 다른 명령을 보낸다.
   */
  analyze(
    startFen: string | null,
    moves: string[],
    opt: AnalysisOptions,
    onInfo: (i: SearchInfo) => void,
  ): AnalysisHandle {
    this.infoCb = onInfo;
    this.setOption("MultiPV", opt.multiPv);
    if (opt.skill) this.setOption("Skill Level", opt.skill.analysis);
    const base = startFen ? `fen ${startFen}` : "startpos";
    this.t.send(`position ${base}${moves.length ? " moves " + moves.join(" ") : ""}`);
    const bestmove = this.waitFor((l) => l.startsWith("bestmove"));
    // searchmoves 는 반드시 마지막에 둔다: Stockfish 는 그 뒤의 토큰을 전부 수 목록으로 읽어 movetime 이 무시된다.
    const sm = opt.searchMoves?.length ? ` searchmoves ${opt.searchMoves.join(" ")}` : "";
    this.t.send(`go movetime ${opt.movetime}${sm}`);
    const done = bestmove.then(() => {
      this.infoCb = null;
      this.setOption("MultiPV", 1);
      if (opt.skill) this.setOption("Skill Level", opt.skill.restore);
    });
    return {
      done,
      stop: () => {
        this.t.send("stop");
        return done;
      },
    };
  }

  stop(): void {
    this.t.send("stop");
  }

  quit(): void {
    this.t.send("quit");
    this.t.terminate();
  }
}
