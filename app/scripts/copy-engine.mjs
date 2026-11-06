// stockfish npm 패키지의 단일 스레드 lite 빌드를 public/engine 으로 복사한다.
// 단일 스레드라 COOP/COEP 헤더가 필요 없고 iOS Safari 에서도 동작한다.
import { copyFileSync, mkdirSync, existsSync } from "node:fs";
import { resolve, dirname } from "node:path";
import { fileURLToPath } from "node:url";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const src = resolve(root, "node_modules/stockfish/bin");
const dst = resolve(root, "public/engine");
mkdirSync(dst, { recursive: true });
for (const f of ["stockfish-19-lite-single.js", "stockfish-19-lite-single.wasm"]) {
  if (!existsSync(resolve(src, f))) throw new Error(`missing ${f} in node_modules/stockfish`);
  copyFileSync(resolve(src, f), resolve(dst, f));
}
console.log("engine files copied to public/engine");
