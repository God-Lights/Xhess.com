# Albatross 브라우저 엔진 (웹사이트에서 쓰기)

Albatross 0.4 부터 엔진을 **파일 하나(`albatross.js`, 약 300 KB)** 로 내려받아 어느 웹사이트에서든 쓸 수 있다.
WebAssembly 가 들어 있어 설치도 별도 파일도 필요 없고, 웹 워커로 실행하면 UCI 로 대화한다. Stockfish.js 와 같은 방식이라
이미 UCI 웹 워커 엔진을 지원하는 코드에는 파일 주소만 바꿔 끼우면 된다.

배포 위치(Cloudflare Pages): **`/engine/`** — 다운로드 버튼, 사용법, 바로 돌려 보는 데모, 버전·SHA-256.

## 쓰는 방법

### 1. 내 사이트에 올려서
```js
const engine = new Worker('albatross.js');           // 내 서버에 올린 같은 출처의 파일
engine.onmessage = (e) => console.log(e.data);        // 한 줄씩: "uciok", "info depth 8 ...", "bestmove e2e4"
engine.postMessage('uci');
engine.postMessage('position startpos moves e2e4 e7e5');
engine.postMessage('go movetime 1000');
```

### 2. 다운로드 없이, 다른 사이트에서
브라우저는 다른 출처의 스크립트로 `new Worker(url)` 을 만들지 못한다(`SecurityError`). 로더가 엔진을 가져와 `blob:` 워커로 시작한다.
```html
<script src="https://<배포 주소>/engine/albatross-loader.js"></script>
<script>
  createAlbatross().then((engine) => {       // engine 은 웹 워커(UCI)
    engine.onmessage = (e) => console.log(e.data);
    engine.postMessage('uci');
    engine.postMessage('go depth 12');
  });
</script>
```
`createAlbatross(url?)` 는 로더 옆의 `albatross.js` 를 가져온다. 배포 사이트는 `/engine/*` 에 `Access-Control-Allow-Origin: *`(CORS)를 열어 둔다(`site/_headers`).
엄격한 Content-Security-Policy 가 있는 페이지는 `worker-src blob:` 과 배포 사이트로의 `connect-src` 가 필요하다.

### 3. Xhess 앱에서
패널의 **엔진 종류 → Albatross 0.4 (자체 엔진)** 으로 바꾼다(웹·모바일·데스크톱 모두). 전략 마킹(MultiPV)과 인간 모드도 그대로 동작한다.
Albatross 에는 난이도(Skill Level) 옵션이 없어 난이도 슬라이더는 꺼진다.

## 지원하는 UCI
`uci` `isready` `ucinewgame` `position` `go`(`depth` `nodes` `movetime` `wtime` `btime` `winc` `binc` `movestogo` `infinite` `searchmoves`) `stop` `quit`,
옵션 `Hash`(1–256 MB, 기본 16)·`MultiPV`(1–32)·`Clear Hash`, 확장 명령 `eval`(항목별 평가).

## 왜 워커가 두 겹인가 (stop 처리)
브라우저의 WebAssembly 탐색은 한 번 시작하면 안쪽에서 중간에 끊을 수 없다(스레드와 공유 메모리가 필요한데, 그러려면 사이트마다
교차 출처 격리(COOP/COEP) 설정이 필요해서 "어느 사이트에서든"과 맞지 않는다). 그래서 같은 파일이 두 역할을 맡는다.

- **supervisor**(페이지가 만든 워커): 페이지와 UCI 로 대화하고, 탐색 중에도 `isready` 에 즉시 답하며 `stop` 을 처리한다.
- **core**(supervisor 가 만든 워커): WebAssembly 엔진을 돌린다. 탐색 중에는 메시지를 받을 수 없다.

`stop` 이 오면 supervisor 가 core 를 종료하고 새로 시작한 뒤 위치와 옵션을 복원하고, 마지막으로 끝낸 반복의 최선수를 `bestmove` 로 돌려준다
(아직 깊이 1도 못 끝냈으면 새 엔진에게 깊이 1 탐색을 시켜 항상 합법 수를 돌려준다). 대가로 **`stop` 후에는 해시 테이블이 비워진다.**

## 알아둘 점
- 단일 스레드. 탐색은 전용 워커에서 돌아 페이지가 멈추지 않는다.
- 필요한 것: Web Worker, WebAssembly, 워커 안의 워커(Chrome·Edge·Firefox 최신, Safari 15 이상).
- 같은 코드의 네이티브 빌드보다 느리다(이 개발 환경에서 노드 기준 약 1.5 M nps 대 2.2 M nps).
- 강도: 이전 버전(0.3)에 +129 Elo. 다른 엔진 대비 절대 강도는 측정하지 않았다. 라이선스 MIT.
- Hash 는 최대 256 MB(WebAssembly 메모리 제한).

## 빌드와 검증
```bash
engine/wasm/build.sh                       # Emscripten 필요 → engine/wasm/dist/albatross.js (+ albatross-loader.js)
node engine/wasm/tests/node_test.cjs engine/wasm/dist/albatross.js engine/build/albatross
node engine/wasm/tests/browser_test.cjs    # Chromium(Playwright): 워커, stop, 다른 출처 로더
node site/build.mjs && node site/test-site.cjs   # 배포 사이트: _headers 의 CORS, 다운로드 페이지, 다른 사이트에서 사용
```
- **배포 파일은 미리 빌드해 커밋한다**(`engine/wasm/dist/`). Cloudflare Pages 빌드에는 Emscripten 이 없기 때문이다. **엔진 소스를 바꾸면 `build.sh` 로 다시 만들어 커밋해야 한다.**
- CI(`wasm` 잡)는 새로 빌드한 파일과 **커밋된 파일 둘 다** 네이티브와 같은 결과(Perft, 고정 깊이 탐색의 노드 수·점수·수순)를 내는지 확인한다. 소스만 바꾸고 배포 파일을 갱신하지 않으면 실패한다.
- 검증 결과: 네이티브와 4개 포지션(깊이 8)에서 노드 수·점수·수순이 정확히 같았고, Perft(5) 4,865,609·Kiwipete(4) 4,085,603 일치.
  브라우저에서 같은 출처 워커·`stop`(탐색 중 `isready` 즉답, 정지 후 옵션·위치 복원, 즉시 정지에도 합법 수)·다른 출처 사이트에서 로더 사용을 확인했다.
- 브라우저 테스트(Playwright)는 CI 에 넣지 않았다(브라우저 설치 비용). 로컬에서 실행한다.
