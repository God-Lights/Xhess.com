// 데스크톱 앱에 번들할 네이티브 Stockfish 를 내려받아 Tauri sidecar 위치에 둔다.
//   src-tauri/binaries/xhess-stockfish-<target-triple>[.exe]   (tauri.conf.json 의 externalBin)
//   src-tauri/binaries/STOCKFISH-COPYING.txt             (GPL-3.0 전문, 리소스로 함께 배포)
// 공식 릴리스 아카이브를 SHA-256 으로 검증한 뒤 필요한 파일만 꺼낸다.
import { createHash } from "node:crypto";
import { execFileSync } from "node:child_process";
import { chmodSync, copyFileSync, existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const TAG = "sf_18";
const BASE = `https://github.com/official-stockfish/Stockfish/releases/download/${TAG}`;

// Rust target triple -> 릴리스 에셋. (x86-64-avx2: 2013년 이후 CPU. 실행 불가 환경은 앱이 WASM 엔진으로 대체한다.)
const ASSETS = {
  "x86_64-unknown-linux-gnu": {
    file: "stockfish-ubuntu-x86-64-avx2.tar",
    sha256: "536c0c2c0cf06450df0bfb5e876ef0d3119950703a8f143627f990c7b5417964",
    inner: "stockfish/stockfish-ubuntu-x86-64-avx2",
    exe: "",
  },
  "x86_64-pc-windows-msvc": {
    file: "stockfish-windows-x86-64-avx2.zip",
    sha256: "6f6c272ebd6ea594377715235c8a7326f75940ef4f4f856f45106028fe6ae900",
    inner: "stockfish/stockfish-windows-x86-64-avx2.exe",
    exe: ".exe",
  },
};

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const outDir = join(root, "src-tauri", "binaries");

function hostTriple() {
  if (process.env.TAURI_ENV_TARGET_TRIPLE) return process.env.TAURI_ENV_TARGET_TRIPLE;
  const out = execFileSync("rustc", ["-vV"], { encoding: "utf8" });
  const m = /^host: (.+)$/m.exec(out);
  if (!m) throw new Error("rustc -vV 에서 host triple 을 찾지 못했습니다");
  return m[1].trim();
}

const triple = hostTriple();
const asset = ASSETS[triple];
if (!asset) {
  console.error(`지원하지 않는 플랫폼: ${triple} (지원: ${Object.keys(ASSETS).join(", ")})`);
  process.exit(1);
}

const dest = join(outDir, `xhess-stockfish-${triple}${asset.exe}`);
// 사이드카 이름에 xhess- 접두사를 붙여 배포판의 stockfish 패키지(/usr/bin/stockfish)와 충돌하지 않게 한다.
const stamp = join(outDir, ".stockfish-version");
const want = `${TAG} ${asset.sha256}`;
if (existsSync(dest) && existsSync(stamp) && readFileSync(stamp, "utf8") === want) {
  console.log(`stockfish ${TAG} 이미 준비됨: ${dest}`);
  process.exit(0);
}

mkdirSync(outDir, { recursive: true });
const tmp = mkdtempSync(join(tmpdir(), "xhess-sf-"));
try {
  console.log(`다운로드: ${BASE}/${asset.file}`);
  const res = await fetch(`${BASE}/${asset.file}`);
  if (!res.ok) throw new Error(`다운로드 실패: HTTP ${res.status}`);
  const buf = Buffer.from(await res.arrayBuffer());
  const sum = createHash("sha256").update(buf).digest("hex");
  if (sum !== asset.sha256) throw new Error(`SHA-256 불일치\n  기대: ${asset.sha256}\n  실제: ${sum}`);

  const archive = join(tmp, asset.file);
  writeFileSync(archive, buf);
  // PATH 의 tar 가 GNU tar 일 수 있어(zip 해제 불가) zip 은 플랫폼 도구로, tar 는 tar 로 푼다.
  if (asset.file.endsWith(".zip")) {
    if (process.platform === "win32") {
      execFileSync("powershell", ["-NoProfile", "-NonInteractive", "-Command",
        `Expand-Archive -LiteralPath '${archive}' -DestinationPath '${tmp}' -Force`], { stdio: "inherit" });
    } else {
      execFileSync("unzip", ["-q", "-o", archive, "-d", tmp], { stdio: "inherit" });
    }
  } else {
    execFileSync("tar", ["-xf", archive, "-C", tmp, asset.inner, "stockfish/Copying.txt"], { stdio: "inherit" });
  }

  copyFileSync(join(tmp, asset.inner), dest);
  if (!asset.exe) chmodSync(dest, 0o755);
  copyFileSync(join(tmp, "stockfish", "Copying.txt"), join(outDir, "STOCKFISH-COPYING.txt"));
  writeFileSync(stamp, want);
  console.log(`설치됨: ${dest}`);
} finally {
  rmSync(tmp, { recursive: true, force: true });
}
