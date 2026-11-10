import { afterEach, describe, expect, it, vi } from "vitest";
import { createBlobWorkerTransport } from "./workerTransport";

class FakeWorker {
  static all: FakeWorker[] = [];
  sent: string[] = [];
  onmessage: ((e: { data: unknown }) => void) | null = null;
  onerror: ((e: { message: string }) => void) | null = null;
  terminated = false;
  constructor(public url: string) {
    FakeWorker.all.push(this);
  }
  postMessage(m: string) {
    this.sent.push(m);
  }
  terminate() {
    this.terminated = true;
  }
}

const flush = () => new Promise((r) => setTimeout(r, 0));

describe("createBlobWorkerTransport", () => {
  afterEach(() => {
    vi.unstubAllGlobals();
    FakeWorker.all = [];
  });

  function stub(fetchImpl: () => Promise<unknown>) {
    vi.stubGlobal("fetch", fetchImpl);
    vi.stubGlobal("Worker", FakeWorker);
    vi.stubGlobal("URL", { createObjectURL: () => "blob:fake" });
  }

  it("파일을 받은 뒤 blob: 워커로 시작하고, 그 전에 보낸 명령은 순서대로 보낸다", async () => {
    stub(async () => ({ ok: true, text: async () => "/* engine */" }));
    const t = createBlobWorkerTransport("./engine/albatross.js");
    t.send("uci");
    t.send("isready");
    await flush();
    const w = FakeWorker.all[0];
    expect(w.url).toBe("blob:fake");
    expect(w.sent).toEqual(["uci", "isready"]);
    t.send("go depth 1");
    expect(w.sent.at(-1)).toBe("go depth 1");
    const lines: string[] = [];
    t.onLine((l) => lines.push(l));
    w.onmessage?.({ data: "uciok\nreadyok" });
    expect(lines).toEqual(["uciok", "readyok"]);
    t.terminate();
    expect(w.terminated).toBe(true);
  });

  it("받기에 실패하면 이유를 info string 으로 알린다", async () => {
    stub(async () => ({ ok: false, status: 404 }));
    const t = createBlobWorkerTransport("./engine/albatross.js");
    const lines: string[] = [];
    t.onLine((l) => lines.push(l));
    await flush();
    expect(lines[0]).toContain("failed to load");
    expect(lines[0]).toContain("HTTP 404");
    expect(FakeWorker.all).toHaveLength(0);
  });

  it("받는 도중 종료하면 워커를 만들지 않는다", async () => {
    stub(async () => ({ ok: true, text: async () => "x" }));
    const t = createBlobWorkerTransport("./engine/albatross.js");
    t.terminate();
    await flush();
    expect(FakeWorker.all).toHaveLength(0);
  });
});
