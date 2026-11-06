# Xhess

크로스 플랫폼 체스 GUI. 모바일은 웹(PWA), PC(Windows/Linux)는 Tauri 프로그램.

```
app/       공통 웹 UI (TypeScript, Vite, chessground, chess.js)
desktop/   Tauri 래퍼 (Windows / Linux)
site/      Cloudflare Pages 설치 사이트 (OS 감지, /play/ 웹 앱)
engine/    Albatross 자체 엔진 (C++20, CMake) — 0.2 First Thought: 탐색(alpha-beta)·평가(HCE)·UCI
```

## 개발

```bash
cd app && npm install && npm run dev      # 브라우저에서 http://localhost:5173
cd app && npm test                        # 단위 테스트
```

데스크톱 (Rust, Linux 는 webkit2gtk 필요):

```bash
cd desktop && npm install && npm run dev      # 개발 창
cd desktop && npm run build                   # 설치 파일 (nsis / deb / AppImage)
```

## 엔진

UI 는 UCI 프로토콜(`app/src/engine/uci.ts`)로만 엔진과 통신하고, 연결 방식은 `Transport` 로 분리돼 있다.

| 환경 | 엔진 | 연결 |
|---|---|---|
| 데스크톱(Windows/Linux) | **로컬 네이티브 Stockfish 18** (멀티스레드, Hash 256MB) | Rust 가 프로세스를 띄워 stdin/stdout 을 Tauri 이벤트로 중계 (`desktop/src-tauri/src/engine.rs`) |
| 데스크톱 + 사용자 지정 | 임의의 UCI 실행 파일 (예: Albatross) | 패널의 "외부 UCI 엔진 경로" 입력 |
| 모바일/웹 | Stockfish WASM (단일 스레드 lite) | Web Worker |

데스크톱에서 로컬 엔진이 실행되지 않으면(예: AVX2 미지원 CPU) 자동으로 WASM 엔진으로 대체된다.
번들 엔진 바이너리는 저장소에 없고, 빌드할 때 `desktop/scripts/fetch-stockfish.mjs` 가 공식 릴리스를
SHA-256 으로 검증해 내려받는다(`npm run fetch-engine`). 설치 파일 크기는 신경망 내장으로 100MB 안팎이다.

> 라이선스: Stockfish 는 GPL-3.0 이다. 번들 시 라이선스 전문과 소스 위치를 함께 배포한다
> (`THIRD_PARTY_NOTICES.md`). Albatross 로 교체하면 이 제약이 사라진다.

## Albatross 엔진 빌드

```bash
cmake -S engine -B engine/build && cmake --build engine/build
ctest --test-dir engine/build            # 단위 테스트
./engine/build/albatross perft 6         # 시작 포지션 perft(6) = 119060324
```

설계와 검증 결과: `engine/docs/0.1-genesis-design.md`, `engine/docs/0.2-first-thought.md`

```bash
./engine/build/albatross bench 6          # 고정 깊이 벤치 (노드 수는 결정적)
# 앱에서 쓰기: 데스크톱 패널의 "외부 UCI 엔진 경로"에 albatross 실행 파일 경로 입력
```
