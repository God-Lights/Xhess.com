// 엔진 분석 수명주기: "추천" 분석(후보 수 여러 개)과 "계획 평가"(내가 그린 수만 평가)를 한 번에 하나씩만 돌린다.
// 새 작업이 시작되면 진행 중이던 작업을 먼저 멈추고, 멈춘 작업의 늦은 결과는 버린다.

import type { AnalysisHandle, SearchInfo, UciEngine } from "./engine/uci";
import { LineCollector, makePlanReport, type Plan, type PlanReport } from "./strategy";

export interface AnalyzerCallbacks {
  /** 추천 분석 진행/완료. fen 은 분석한 국면 */
  onLines(lines: SearchInfo[], fen: string, final: boolean): void;
  /** 계획 평가 결과. planInfo/bestInfo 는 보드 도형을 그리는 데 쓴다 */
  onPlan(report: PlanReport, planInfo: SearchInfo, bestInfo: SearchInfo, fen: string): void;
}

export interface AnalyzerOptions {
  /** 분석이 끝난 뒤 엔진에 되돌릴 Skill Level (대국 난이도 슬라이더 값) */
  skillRestore(): number;
  recommendMs?: number;
  planMs?: number;
  /** UI 갱신 최소 간격(ms) */
  throttleMs?: number;
}

/** 분석 도중의 1순위를 비교 기준으로 재사용할 최소 깊이 */
const MIN_REUSE_DEPTH = 10;

export class Analyzer {
  private current: AnalysisHandle | null = null;
  private stopping: Promise<void> = Promise.resolve();
  private token = 0;
  private cachedFen: string | null = null;
  private cachedBest: SearchInfo | null = null;

  constructor(
    private getEngine: () => UciEngine | null,
    private cb: AnalyzerCallbacks,
    private opt: AnalyzerOptions,
  ) {}

  /** 진행 중인 분석을 멈추고 엔진이 다시 쓸 수 있을 때까지 기다린다. 이후 도착하는 결과는 무시된다. */
  async cancel(): Promise<void> {
    this.token++;
    const h = this.current;
    this.current = null;
    // 이미 다른 호출이 멈추는 중이어도 그 종료를 함께 기다린다(엔진이 완전히 멈추기 전에 다음 명령을 보내지 않도록).
    if (h) this.stopping = h.stop();
    await this.stopping;
  }

  forget(): void {
    this.cachedFen = null;
    this.cachedBest = null;
  }

  private skill() {
    return { analysis: 20, restore: this.opt.skillRestore() };
  }

  /** 후보 수 3개(MultiPV)를 분석해 진행 상황을 계속 알린다. */
  async recommend(uciMoves: string[], fen: string): Promise<void> {
    await this.cancel();
    const engine = this.getEngine();
    if (!engine) return;
    const my = this.token;
    const collector = new LineCollector();
    const throttle = this.opt.throttleMs ?? 250;
    let last = 0;

    const handle = engine.analyze(
      null,
      uciMoves,
      { multiPv: 3, movetime: this.opt.recommendMs ?? 3000, skill: this.skill() },
      (info) => {
        if (my !== this.token) return;
        collector.update(info);
        // 분석 도중에 계획 평가가 끼어들어도 이미 깊이 구한 1순위를 비교 기준으로 쓸 수 있게 계속 보관한다.
        const top = collector.lines()[0];
        if (top) {
          this.cachedFen = fen;
          this.cachedBest = top;
        }
        const now = Date.now();
        if (now - last >= throttle) {
          last = now;
          this.cb.onLines(collector.lines(), fen, false);
        }
      },
    );
    this.current = handle;
    await handle.done;
    if (my !== this.token) return; // 도중에 취소/교체됨
    this.current = null;
    this.cb.onLines(collector.lines(), fen, true);
  }

  /** 내 계획의 첫 수만 평가하고(searchmoves), 엔진 1순위와 비교한 보고서를 만든다. */
  async evaluatePlan(uciMoves: string[], fen: string, plan: Plan): Promise<void> {
    if (plan.moves.length === 0) return;
    await this.cancel();
    const engine = this.getEngine();
    if (!engine) return;
    const my = this.token;

    // 비교 기준이 되는 엔진 1순위: 충분히 깊게 분석된 값이 있으면 재사용하고, 없으면 먼저 짧게 구한다.
    let best = this.cachedFen === fen && this.cachedBest && this.cachedBest.depth >= MIN_REUSE_DEPTH ? this.cachedBest : null;
    if (!best) {
      let latest: SearchInfo | null = null;
      const h = engine.analyze(null, uciMoves, { multiPv: 1, movetime: this.opt.planMs ?? 1500, skill: this.skill() }, (i) => {
        if (my === this.token && i.multipv === 1) latest = i;
      });
      this.current = h;
      await h.done;
      if (my !== this.token) return;
      this.current = null;
      best = latest;
      if (best) {
        this.cachedFen = fen;
        this.cachedBest = best;
      }
    }
    if (!best) return;

    let planInfo: SearchInfo | null = null;
    const h2 = engine.analyze(
      null,
      uciMoves,
      { multiPv: 1, searchMoves: [plan.moves[0].uci], movetime: this.opt.planMs ?? 1500, skill: this.skill() },
      (i) => {
        if (my === this.token && i.multipv === 1) planInfo = i;
      },
    );
    this.current = h2;
    await h2.done;
    if (my !== this.token) return;
    this.current = null;
    if (!planInfo) return;
    const info: SearchInfo = planInfo;
    this.cb.onPlan(makePlanReport(fen, plan, info, best), info, best, fen);
  }
}
