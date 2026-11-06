import { Chess, type Square, type Move } from "chess.js";

export type Color = "white" | "black";
export type Key = Square;

export interface Outcome {
  over: boolean;
  /** 사용자에게 보여줄 결과 문구 */
  text: string;
}

/** chess.js 를 감싼 대국 상태. UI 와 엔진 어느 쪽에도 의존하지 않는다. */
export class Game {
  private chess = new Chess();
  private undone: Move[] = [];

  reset(): void {
    this.chess = new Chess();
    this.undone = [];
  }

  get fen(): string {
    return this.chess.fen();
  }

  get turn(): Color {
    return this.chess.turn() === "w" ? "white" : "black";
  }

  get history(): Move[] {
    return this.chess.history({ verbose: true });
  }

  get uciMoves(): string[] {
    return this.history.map((m) => m.from + m.to + (m.promotion ?? ""));
  }

  get lastMove(): [Key, Key] | undefined {
    const h = this.history;
    const m = h[h.length - 1];
    return m ? [m.from, m.to] : undefined;
  }

  get inCheck(): boolean {
    return this.chess.inCheck();
  }

  get pgn(): string {
    return this.chess.pgn();
  }

  dests(): Map<Key, Key[]> {
    const map = new Map<Key, Key[]>();
    for (const m of this.chess.moves({ verbose: true })) {
      const list = map.get(m.from) ?? [];
      list.push(m.to);
      map.set(m.from, list);
    }
    return map;
  }

  isPromotion(from: Key, to: Key): boolean {
    return this.chess
      .moves({ verbose: true })
      .some((m) => m.from === from && m.to === to && m.promotion);
  }

  move(from: Key, to: Key, promotion?: string): Move | null {
    try {
      const m = this.chess.move({ from, to, promotion });
      this.undone = [];
      return m;
    } catch {
      return null;
    }
  }

  moveUci(uci: string): Move | null {
    return this.move(uci.slice(0, 2) as Key, uci.slice(2, 4) as Key, uci[4]);
  }

  undo(): Move | null {
    const m = this.chess.undo();
    if (m) this.undone.push(m);
    return m;
  }

  outcome(): Outcome {
    const c = this.chess;
    if (c.isCheckmate()) {
      return { over: true, text: `체크메이트 — ${c.turn() === "w" ? "흑" : "백"} 승리` };
    }
    if (c.isStalemate()) return { over: true, text: "스테일메이트 — 무승부" };
    if (c.isInsufficientMaterial()) return { over: true, text: "기물 부족 — 무승부" };
    if (c.isThreefoldRepetition()) return { over: true, text: "3회 동형 반복 — 무승부" };
    if (c.isDraw()) return { over: true, text: "50수 규칙 — 무승부" };
    return { over: false, text: "" };
  }
}
