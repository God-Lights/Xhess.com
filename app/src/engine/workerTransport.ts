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

/**
 * Albatross 용: 파일을 fetch 해서 blob: URL 워커로 시작한다.
 * Albatross 는 워커(supervisor)가 다시 코어 워커를 만드는 구조다. 페이지가 `new Worker(url)` 로 직접 띄우면
 * 코어 워커도 같은 URL 에서 다시 받아야 하는데, 데스크톱 앱(Windows WebView2 의 tauri 커스텀 프로토콜)에서는
 * 워커 안에서 시작한 요청이 처리되지 않아 코어가 뜨지 않았다("응답 없음"). blob: URL 이면 이 요청이 필요 없다.
 * 워커는 비동기로 만들어지므로 그 전에 보낸 명령은 모아 두었다가 보낸다.
 */
export function createBlobWorkerTransport(url: string): Transport {
  let w: Worker | null = null;
  let closed = false;
  const pending: string[] = [];
  let cb: (line: string) => void = () => {};
  const deliver = (data: unknown) => {
    for (const line of (typeof data === "string" ? data : String(data)).split("\n")) if (line) cb(line.trim());
  };
  fetch(url)
    .then((res) => {
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      return res.text();
    })
    .then((code) => {
      if (closed) return;
      const blobUrl = URL.createObjectURL(new Blob([code], { type: "text/javascript" }));
      w = new Worker(blobUrl);
      w.onmessage = (e: MessageEvent) => deliver(e.data);
      w.onerror = (e: ErrorEvent) => deliver(`info string albatross: worker error: ${e.message}`);
      for (const m of pending.splice(0)) w.postMessage(m);
    })
    .catch((err) => deliver(`info string albatross: failed to load ${url}: ${err instanceof Error ? err.message : err}`));
  return {
    send: (c) => (w ? w.postMessage(c) : pending.push(c)),
    onLine: (f) => (cb = f),
    terminate: () => {
      closed = true;
      w?.terminate();
    },
  };
}

export const STOCKFISH_URL = `${import.meta.env.BASE_URL}engine/stockfish-19-lite-single.js`;

/** 자체 엔진 Albatross (WebAssembly, 파일 하나). engine/wasm/build.sh 가 만든 것을 복사해 쓴다. */
export const ALBATROSS_URL = `${import.meta.env.BASE_URL}engine/albatross.js`;
