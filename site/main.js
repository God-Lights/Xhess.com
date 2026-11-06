// OS 를 감지해 맞는 설치 파일을 안내한다. 설치 파일은 GitHub Releases 에서 받는다
// (Cloudflare Pages 는 파일당 25MB 제한이 있어 설치 파일을 직접 호스팅하지 않는다).
const REPO = "god-lights/xhess.com";
const RELEASES = `https://github.com/${REPO}/releases/latest`;

// 릴리스 에셋 파일명 → 슬롯
const KINDS = [
  { slot: "win",   label: "설치 프로그램 (.exe)", test: /x64-setup\.exe$/i },
  { slot: "linux", label: ".deb (Ubuntu/Debian)", test: /_amd64\.deb$/i },
  { slot: "linux", label: "AppImage",             test: /_amd64\.AppImage$/i },
];

function detect() {
  const ua = navigator.userAgent || "";
  if (/iPhone|iPad|iPod/i.test(ua) || (/Macintosh/.test(ua) && navigator.maxTouchPoints > 1)) return "ios";
  if (/Android/i.test(ua)) return "android";
  if (/Windows/i.test(ua)) return "win";
  if (/Linux|X11/i.test(ua)) return "linux";
  if (/Mac/i.test(ua)) return "mac";
  return "other";
}

function btn(text, href, ghost) {
  const a = document.createElement("a");
  a.className = "btn" + (ghost ? " ghost" : "");
  a.textContent = text; a.href = href; a.rel = "noopener";
  return a;
}

function renderPrimary(os, links) {
  const title = document.getElementById("primaryTitle");
  const desc = document.getElementById("primaryDesc");
  const box = document.getElementById("primaryActions");
  box.replaceChildren();
  if (os === "ios" || os === "android") {
    title.textContent = os === "ios" ? "아이폰에서 시작하기" : "안드로이드에서 시작하기";
    desc.textContent = "설치 없이 웹에서 바로 플레이하고, 홈 화면에 추가해 앱처럼 쓰세요.";
    box.append(btn("웹에서 플레이", "play/"));
    return;
  }
  if (os === "win" || os === "linux") {
    const mine = links.filter((l) => l.slot === os);
    title.textContent = os === "win" ? "Windows 용 Xhess 다운로드" : "Linux 용 Xhess 다운로드";
    if (mine.length) {
      desc.textContent = "프로그램을 설치하면 오프라인에서도 플레이할 수 있습니다.";
      mine.forEach((l, i) => box.append(btn(l.label, l.url, i > 0)));
    } else {
      desc.textContent = "아직 배포된 설치 파일이 없습니다. 지금은 웹 버전을 이용하세요.";
      box.append(btn("웹에서 플레이", "play/"));
    }
    return;
  }
  title.textContent = "Xhess 시작하기";
  desc.textContent = "이 기기용 설치 파일은 아직 없습니다. 웹 버전을 이용하세요.";
  box.append(btn("웹에서 플레이", "play/"));
}

function renderSlots(links) {
  for (const el of document.querySelectorAll("[data-slot]")) {
    const mine = links.filter((l) => l.slot === el.dataset.slot);
    el.replaceChildren();
    if (!mine.length) {
      const b = btn("준비 중", "#", true); b.setAttribute("aria-disabled", "true"); el.append(b);
    } else mine.forEach((l, i) => el.append(btn(l.label, l.url, i > 0)));
  }
}

async function loadLinks() {
  try {
    const r = await fetch(`https://api.github.com/repos/${REPO}/releases/latest`, {
      headers: { Accept: "application/vnd.github+json" },
    });
    if (!r.ok) throw new Error(r.status);
    const rel = await r.json();
    const links = [];
    for (const k of KINDS) {
      const a = (rel.assets || []).find((x) => k.test.test(x.name));
      if (a) links.push({ slot: k.slot, label: k.label, url: a.browser_download_url });
    }
    document.getElementById("versionLine").textContent = `최신 버전 ${rel.tag_name}`;
    return links;
  } catch {
    return [];
  }
}

const os = detect();
renderPrimary(os, []);
loadLinks().then((links) => { renderPrimary(os, links); renderSlots(links); });
