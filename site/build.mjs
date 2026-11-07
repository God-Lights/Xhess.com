// Cloudflare Pages 출력물(site/dist)을 만든다.
//   /        설치 안내 페이지 (site/*)
//   /play/   웹 앱 (app/dist)
//   /engine/ Albatross 브라우저 엔진 다운로드 페이지와 파일(albatross.js, 로더)
// 먼저 `npm --prefix app run build` 가 실행되어 있어야 한다.
import { createHash } from "node:crypto";
import { cpSync, rmSync, mkdirSync, existsSync, readFileSync, writeFileSync } from "node:fs";
import { resolve, dirname } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const dist = resolve(here, "dist");
const appDist = resolve(here, "../app/dist");
if (!existsSync(appDist)) throw new Error("app/dist 없음 — 먼저 `npm --prefix app run build` 를 실행하세요");

rmSync(dist, { recursive: true, force: true });
mkdirSync(dist, { recursive: true });
for (const f of ["index.html", "style.css", "main.js", "favicon.png", "logo.png", "_headers"]) {
  cpSync(resolve(here, f), resolve(dist, f));
}
cpSync(appDist, resolve(dist, "play"), { recursive: true });

// ---- Albatross 브라우저 엔진: 미리 빌드해 커밋해 둔 단일 파일(engine/wasm/build.sh)을 배포한다 ----
const engineDist = resolve(here, "../engine/wasm/dist");
const engineJs = resolve(engineDist, "albatross.js");
const loaderJs = resolve(engineDist, "albatross-loader.js");
if (!existsSync(engineJs) || !existsSync(loaderJs)) throw new Error("engine/wasm/dist 에 albatross.js 가 없습니다 (engine/wasm/build.sh 로 빌드)");
const version = /project\(Albatross VERSION (\d+\.\d+\.\d+)/.exec(readFileSync(resolve(here, "../engine/CMakeLists.txt"), "utf8"))?.[1];
if (!version) throw new Error("engine/CMakeLists.txt 에서 버전을 찾지 못했습니다");
const code = readFileSync(engineJs);
const sha256 = createHash("sha256").update(code).digest("hex");
const outEngine = resolve(dist, "engine");
mkdirSync(outEngine, { recursive: true });
cpSync(engineJs, resolve(outEngine, "albatross.js"));
cpSync(engineJs, resolve(outEngine, `albatross-${version}.js`)); // 버전을 고정해서 쓰고 싶은 사이트용
cpSync(loaderJs, resolve(outEngine, "albatross-loader.js"));
writeFileSync(resolve(outEngine, "latest.json"), JSON.stringify({ version, file: "albatross.js", bytes: code.length, sha256 }, null, 2) + "\n");
const page = readFileSync(resolve(here, "engine/index.html"), "utf8")
  .replaceAll("__VERSION__", version)
  .replaceAll("__SHA256__", sha256)
  .replaceAll("__SIZE__", String(Math.round(code.length / 1024)));
writeFileSync(resolve(outEngine, "index.html"), page);
console.log("site built ->", dist);
