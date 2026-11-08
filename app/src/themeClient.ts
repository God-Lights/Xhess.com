// 테마 계산 전용 Albatross 워커. 대국용 엔진(Stockfish 든 Albatross 든)과 별개라서, 어떤 엔진으로 두든 같은 평가 기준의
// 테마를 볼 수 있다. 탐색이 필요 없어 한 포지션에 수 ms 면 끝난다.

import type { Transport } from "./engine/uci";
import { parseThemes, type ThemeReport } from "./themes";

export interface ThemeClientOptions {
  /** 엔진 시작(uciok)과 한 번의 응답을 기다리는 최대 시간(ms) */
  timeoutMs?: number;
}

export class ThemeClient {
  private transport: Transport | null = null;
  private lines: string[] = [];
  private waiters: Array<() => void> = [];
  private ready: Promise<void> | null = null;
  private chain: Promise<unknown> = Promise.resolve();
  private latest = 0;

  constructor(
    private make: () => Transport,
    private opt: ThemeClientOptions = {},
  ) {}

  private timeout() {
    return this.opt.timeoutMs ?? 15000;
  }

  private waitFor(pred: (l: string) => boolean): Promise<string> {
    return new Promise((res, rej) => {
      const t = setTimeout(() => {
        this.waiters.splice(this.waiters.indexOf(check), 1);
        rej(new Error("테마 엔진이 응답하지 않습니다"));
      }, this.timeout());
      const check = () => {
        const l = this.lines.find(pred);
        if (l === undefined) return;
        clearTimeout(t);
        this.waiters.splice(this.waiters.indexOf(check), 1);
        res(l);
      };
      this.waiters.push(check);
      check();
    });
  }

  private start(): Promise<void> {
    if (!this.ready) {
      this.ready = (async () => {
        const t = this.make();
        this.transport = t;
        t.onLine((l) => {
          this.lines.push(l);
          for (const w of [...this.waiters]) w();
        });
        t.send("uci");
        await this.waitFor((l) => l === "uciok");
      })();
      // 시작에 실패하면 다음 요청에서 다시 시도할 수 있게 한다.
      this.ready.catch(() => {
        this.transport?.terminate();
        this.transport = null;
        this.ready = null;
      });
    }
    return this.ready;
  }

  /**
   * fen 의 테마를 구한다. 요청이 겹치면 가장 마지막 요청만 계산하고 앞선 요청은 null 을 돌려준다.
   * 엔진을 불러오지 못하거나 응답이 없으면 오류를 던진다.
   */
  request(fen: string): Promise<ThemeReport | null> {
    const id = ++this.latest;
    const run = async (): Promise<ThemeReport | null> => {
      if (id !== this.latest) return null; // 그 사이 더 새로운 요청이 들어왔다
      await this.start();
      if (id !== this.latest) return null;
      this.lines.length = 0;
      this.transport!.send(`position fen ${fen}`);
      this.transport!.send("themes");
      await this.waitFor((l) => l.startsWith("themes done"));
      return parseThemes(this.lines);
    };
    const result = this.chain.then(run, run);
    this.chain = result.catch(() => {});
    return result;
  }

  dispose(): void {
    this.latest++;
    this.transport?.terminate();
    this.transport = null;
    this.ready = null;
  }
}
