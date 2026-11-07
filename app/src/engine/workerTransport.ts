import type { Transport } from "./uci";

/** 웹/모바일(및 현재 데스크톱): Web Worker 안의 Stockfish WASM. */
export function createWorkerTransport(url: string): Transport {
  const w = new Worker(url);
  let cb: (line: string) => void = () => {};
  w.onmessage = (e: MessageEvent) => {
    const data = typeof e.data === "string" ? e.data : String(e.data);
    for (const line of data.split("\n")) if (line) cb(line.trim());
  };
  return {
    send: (c) => w.postMessage(c),
    onLine: (f) => (cb = f),
    terminate: () => w.terminate(),
  };
}

export const STOCKFISH_URL = `${import.meta.env.BASE_URL}engine/stockfish-19-lite-single.js`;

/** 자체 엔진 Albatross (WebAssembly, 파일 하나). engine/wasm/build.sh 가 만든 것을 복사해 쓴다. */
export const ALBATROSS_URL = `${import.meta.env.BASE_URL}engine/albatross.js`;
