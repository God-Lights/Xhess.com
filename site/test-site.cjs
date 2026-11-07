// 빌드된 사이트(site/dist)를 실제 _headers 규칙을 적용해 서빙하고, 브라우저로 검증한다.
//   node site/test-site.cjs        (playwright 필요, 먼저 `node site/build.mjs`)
const http = require('http'), fs = require('fs'), path = require('path');
const { chromium } = require('playwright');
const dist = path.resolve(__dirname, 'dist');

// _headers 파싱(Cloudflare Pages 형식): 경로 패턴(* 와일드카드) 다음 줄들이 그 경로의 헤더
function parseHeaders(text) {
  const rules = []; let cur = null;
  for (const raw of text.split('\n')) {
    if (!raw.trim() || raw.trim().startsWith('#')) continue;
    if (/^\S/.test(raw)) { cur = { re: new RegExp('^' + raw.trim().replace(/[.+?^${}()|[\]\\]/g, '\\$&').replace(/\*/g, '.*') + '$'), h: {} }; rules.push(cur); }
    else if (cur) { const i = raw.indexOf(':'); cur.h[raw.slice(0, i).trim()] = raw.slice(i + 1).trim(); }
  }
  return rules;
}
const rules = parseHeaders(fs.readFileSync(path.join(dist, '_headers'), 'utf8'));
const types = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.json': 'application/json', '.png': 'image/png', '.wasm': 'application/wasm' };

const serve = (port, root, applyRules) => new Promise((res) => {
  const s = http.createServer((req, resp) => {
    let p = decodeURIComponent(req.url.split('?')[0]); if (p.endsWith('/')) p += 'index.html';
    fs.readFile(path.join(root, p), (err, data) => {
      if (err) { resp.writeHead(404); return resp.end(); }
      const h = { 'content-type': types[path.extname(p)] || 'application/octet-stream' };
      if (applyRules) for (const r of rules) if (r.re.test(p)) Object.assign(h, Object.fromEntries(Object.entries(r.h).map(([k, v]) => [k.toLowerCase(), v])));
      resp.writeHead(200, h); resp.end(data);
    });
  }).listen(port, () => res(s));
});

let failures = 0;
const check = (c, m) => { if (!c) { failures++; console.error('  FAIL', m); } else console.log('  ok  ', m); };
const SITE = 8811, OTHER = 8812;

(async () => {
  const site = await serve(SITE, dist, true);
  // 완전히 다른 사이트: 엔진 파일 없이 배포된 사이트의 로더만 참조한다.
  const otherRoot = fs.mkdtempSync(path.join(require('os').tmpdir(), 'other-site-'));
  fs.writeFileSync(path.join(otherRoot, 'index.html'), `<!doctype html><title>my chess blog</title><script src="http://localhost:${SITE}/engine/albatross-loader.js"></script>`);
  const other = await serve(OTHER, otherRoot, false);
  const browser = await chromium.launch();
  try {
    // 응답 헤더
    const head = await (await fetch(`http://localhost:${SITE}/engine/albatross.js`)).headers;
    check(head.get('access-control-allow-origin') === '*', 'CORS: Access-Control-Allow-Origin: *');
    check(/javascript/.test(head.get('content-type')), `content-type ${head.get('content-type')}`);
    const meta = await (await fetch(`http://localhost:${SITE}/engine/latest.json`)).json();
    check(/^\d+\.\d+\.\d+$/.test(meta.version) && /^[0-9a-f]{64}$/.test(meta.sha256), `latest.json ${meta.version} ${meta.sha256.slice(0, 12)}…`);
    const bytes = fs.readFileSync(path.join(dist, 'engine/albatross.js'));
    check(require('crypto').createHash('sha256').update(bytes).digest('hex') === meta.sha256, 'SHA-256 이 파일과 일치');
    check(fs.existsSync(path.join(dist, `engine/albatross-${meta.version}.js`)), '버전 고정 파일 존재');

    // 다운로드 페이지: 값 채움 + 데모
    const ctx = await browser.newContext(); const page = await ctx.newPage();
    const errs = []; page.on('pageerror', (e) => errs.push(e.message)); page.on('console', (m) => { if (m.type() === 'error') errs.push(m.text()); });
    await page.goto(`http://localhost:${SITE}/engine/`);
    const text = await page.locator('body').innerText();
    check(!/__VERSION__|__SHA256__|__SIZE__/.test(await page.content()), '자리표시자가 모두 치환됨');
    check(text.includes(meta.sha256), 'SHA-256 표시');
    check((await page.locator('.origin').first().innerText()) === `http://localhost:${SITE}`, '사용 예시에 실제 주소가 채워짐');
    await page.click('#demoBtn');
    await page.waitForFunction(() => /bestmove/.test(document.getElementById('demoOut').textContent), null, { timeout: 30000 });
    const demo = await page.locator('#demoOut').innerText();
    check(/id name Albatross/.test(demo) && /info depth/.test(demo) && /bestmove [a-h][1-8][a-h][1-8]/.test(demo), `페이지 데모가 엔진을 실행함 (${demo.split('\n').filter((l) => l.startsWith('bestmove'))[0]})`);
    check(errs.length === 0, `페이지 오류 없음 ${errs.join('; ')}`);
    await ctx.close();

    // 다른 사이트에서 사용
    const ctx2 = await browser.newContext(); const p2 = await ctx2.newPage();
    const errs2 = []; p2.on('pageerror', (e) => errs2.push(e.message)); p2.on('console', (m) => { if (m.type() === 'error') errs2.push(m.text()); });
    await p2.goto(`http://localhost:${OTHER}/`);
    const r = await p2.evaluate(async () => {
      const worker = await createAlbatross();
      const lines = [];
      worker.onmessage = (e) => lines.push(e.data);
      const wait = (pred) => new Promise((res, rej) => { const t = setTimeout(() => rej(new Error('timeout')), 20000); const i = setInterval(() => { const l = lines.find(pred); if (l) { clearTimeout(t); clearInterval(i); res(l); } }, 20); });
      worker.postMessage('uci'); await wait((l) => l === 'uciok');
      worker.postMessage('setoption name MultiPV value 3');
      worker.postMessage('position startpos moves e2e4 e7e5');
      worker.postMessage('go depth 6');
      const best = await wait((l) => l.startsWith('bestmove'));
      return { origin: location.origin, best, multipv3: lines.some((l) => / multipv 3 /.test(l)) };
    });
    check(r.origin === `http://localhost:${OTHER}`, `다른 사이트(${r.origin})에서 실행`);
    check(/^bestmove /.test(r.best) && r.multipv3, `배포된 엔진을 로더로 받아 사용: ${r.best}, MultiPV 3 동작`);
    check(errs2.length === 0, `다른 사이트 페이지 오류 없음 ${errs2.join('; ')}`);
    await ctx2.close();

    // 랜딩 페이지 링크
    const ctx3 = await browser.newContext(); const p3 = await ctx3.newPage();
    await p3.goto(`http://localhost:${SITE}/`);
    check((await p3.locator('a[href="engine/"]').count()) === 1, '랜딩 페이지에 엔진 다운로드 링크');
    await p3.screenshot({ path: process.argv[2] ? path.join(process.argv[2], 'site-landing.png') : '/dev/null' }).catch(() => {});
    await p3.goto(`http://localhost:${SITE}/engine/`);
    await p3.setViewportSize({ width: 900, height: 1400 });
    if (process.argv[2]) await p3.screenshot({ path: path.join(process.argv[2], 'engine-page.png'), fullPage: true });
    await ctx3.close();
  } finally { await browser.close(); site.close(); other.close(); }
  console.log(failures ? `\n${failures} FAILED` : '\nall passed');
  process.exit(failures ? 1 : 0);
})().catch((e) => { console.error('FAIL', e); process.exit(1); });
