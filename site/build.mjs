// Cloudflare Pages 출력물(site/dist)을 만든다.
//   /        설치 안내 페이지 (site/*)
//   /play/   웹 앱 (app/dist)
// 먼저 `npm --prefix app run build` 가 실행되어 있어야 한다.
import { cpSync, rmSync, mkdirSync, existsSync } from "node:fs";
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
console.log("site built ->", dist);
