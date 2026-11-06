# Xhess

크로스 플랫폼 체스 GUI. 모바일은 웹(PWA), PC(Windows/Linux)는 Tauri 프로그램.

```
app/       공통 웹 UI (TypeScript, Vite, chessground, chess.js)
desktop/   Tauri 래퍼 (Windows / Linux)
site/      Cloudflare Pages 설치 사이트 (OS 감지, /play/ 웹 앱)
engine/    Albatross 자체 엔진 (C++20, CMake) — 0.1 Genesis: 규칙/수 생성/Perft/최소 UCI
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

UI 는 UCI 프로토콜(`app/src/engine/uci.ts`)로만 엔진과 통신한다. 현재는 Stockfish 19 lite
(단일 스레드 WASM)를 Web Worker 로 실행한다. 이후 Albatross 가 같은 `Transport` 인터페이스로 교체된다.

> 라이선스 주의: Stockfish 는 GPL-3.0 이다. Stockfish 를 포함해 배포하는 빌드는 GPL-3.0 조건을
> 따라야 한다. Albatross 로 교체하면 이 제약이 사라진다.

## Albatross 엔진 빌드

```bash
cmake -S engine -B engine/build && cmake --build engine/build
ctest --test-dir engine/build            # 단위 테스트
./engine/build/albatross perft 6         # 시작 포지션 perft(6) = 119060324
```

설계와 검증 결과: `engine/docs/0.1-genesis-design.md`
