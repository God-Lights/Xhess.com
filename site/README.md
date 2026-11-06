# Xhess 설치 사이트 (Cloudflare Pages)

`/` 설치 안내(OS 자동 감지) · `/play/` 웹 앱(PWA). 설치 파일은 GitHub Releases 에서 내려받는다.

## Cloudflare Pages 연결 (대시보드, 권장)

Workers & Pages → Create → Pages → Connect to Git → `god-lights/xhess.com`

| 항목 | 값 |
|---|---|
| Production branch | `main` |
| Build command | `npm --prefix app ci && npm --prefix app run build && node site/build.mjs` |
| Build output directory | `site/dist` |
| 환경 변수 | `NODE_VERSION` = `22` |

푸시할 때마다 자동 배포되고, 브랜치/PR 마다 미리보기 URL 이 생성된다.

## 수동 배포

```bash
cd site && npm run build && npx wrangler pages deploy dist --project-name xhess
```

## 설치 파일 배포

```bash
git tag v0.1.0 && git push origin v0.1.0     # release.yml 이 빌드 후 GitHub Release 생성
```

사이트가 `releases/latest` 의 에셋 이름(`*x64-setup.exe`, `*_amd64.deb`, `*_amd64.AppImage`)으로
링크를 찾는다. 에셋 이름 규칙을 바꾸면 `site/main.js` 의 `KINDS` 도 같이 고쳐야 한다.

## 주의

- GitHub 저장소가 **private** 이면 릴리스 API/다운로드가 익명으로 막혀 링크가 나오지 않는다.
  저장소를 public 으로 하거나, 설치 파일을 Cloudflare R2 에 올리는 방식으로 바꿔야 한다.
- 서비스 워커(`/play/sw.js`)는 stale-while-revalidate 캐시다. 배포 후 새 버전은 두 번째 방문부터 보인다.
