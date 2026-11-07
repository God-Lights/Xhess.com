// 실제 브라우저(Chromium)에서 워커 구조와 "어느 사이트에서든" 사용을 검증한다.
//   node engine/wasm/tests/browser_test.cjs [dist 디렉터리]      (playwright 필요)
// 엔진 호스트(포트 A, CORS 허용)와 전혀 다른 출처의 페이지(포트 B)를 띄워 로더로 엔진을 쓴다.
const http = require('http'), fs = require('fs'), path = require('path');
const { chromium } = require('playwright');
const dist = path.resolve(process.argv[2] || path.join(__dirname, '../dist'));

let failures = 0;
const check = (cond, msg) => { if (!cond) { failures++; console.error('  FAIL', msg); } else console.log('  ok  ', msg); };

const serve = (port, handler) => new Promise((res) => { const s = http.createServer(handler).listen(port, () => res(s)); });
const A = 8801, B = 8802;

// A: 엔진 호스트. 다른 출처의 페이지가 가져갈 수 있게 CORS 를 연다(배포 사이트의 _headers 와 같은 설정).
const hostA = () => serve(A, (req, res) => {
  const f = path.join(dist, decodeURIComponent(req.url.split('?')[0]).replace(/^\//, '') || 'index.html');
  fs.readFile(f, (err, data) => {
    if (err) { res.writeHead(404); return res.end(); }
    res.writeHead(200, { 'content-type': 'text/javascript', 'access-control-allow-origin': '*' });
    res.end(data);
  });
});
// B: 전혀 다른 사이트(다른 포트 = 다른 출처). 엔진 파일은 갖고 있지 않다.
const hostB = () => serve(B, (req, res) => {
  res.writeHead(200, { 'content-type': 'text/html' });
  res.end(`<!doctype html><title>other site</title><script src="http://localhost:${A}/albatross-loader.js"></script>`);
});

// 페이지 안에서 쓰는 UCI 헬퍼: 워커에 명령을 보내고 조건이 맞는 줄까지 모은다.
const helper = `
window.uci = (worker) => {
  const lines = []; const waiters = [];
  worker.onmessage = (e) => { lines.push(e.data); waiters.slice().forEach((w) => w()); };
  const wait = (pred, ms = 20000) => new Promise((res, rej) => {
    const t = setTimeout(() => rej(new Error('timeout waiting for ' + pred)), ms);
    const check = () => { const l = lines.find(pred); if (l !== undefined) { clearTimeout(t); waiters.splice(waiters.indexOf(check), 1); res(l); } };
    waiters.push(check); check();
  });
  return { lines, send: (c) => worker.postMessage(c), wait, worker };
};`;

(async () => {
  const sa = await hostA(), sb = await hostB();
  const browser = await chromium.launch();
  try {
    // ── 같은 출처: new Worker('albatross.js') ──
    const ctxA = await browser.newContext(); const pa = await ctxA.newPage();
    const errs = []; pa.on('pageerror', (e) => errs.push(e.message));
    await pa.goto(`http://localhost:${A}/albatross-loader.js`); // 같은 출처 페이지(내용은 무관)
    await pa.evaluate(helper);
    console.log('same-origin worker');
    const r1 = await pa.evaluate(async () => {
      const e = window.uci(new Worker('albatross.js'));
      e.send('uci');
      const idLine = await e.wait((l) => l.startsWith('id name'));
      await e.wait((l) => l === 'uciok');
      e.send('isready'); await e.wait((l) => l === 'readyok');
      e.send('position startpos moves e2e4 e7e5'); e.send('go depth 6');
      const best = await e.wait((l) => l.startsWith('bestmove'));
      return { idLine, best, infos: e.lines.filter((l) => l.startsWith('info depth')).length };
    });
    check(/Albatross 0\.4/.test(r1.idLine), `핸드셰이크: ${r1.idLine}`);
    check(/^bestmove [a-h][1-8][a-h][1-8]/.test(r1.best) && r1.infos >= 6, `go depth 6 → ${r1.best} (${r1.infos} info lines)`);

    // stop 이 탐색 중에도 즉시 먹는다(워커 재시작) + isready 즉답 + 상태 복원
    console.log('stop during a search');
    const r2 = await pa.evaluate(async () => {
      const e = window.uci(new Worker('albatross.js'));
      e.send('uci'); await e.wait((l) => l === 'uciok');
      e.send('setoption name MultiPV value 2');
      e.send('position startpos moves e2e4');
      e.send('go infinite');
      await e.wait((l) => l.startsWith('info depth 4 '));
      const t0 = performance.now();
      e.send('isready');
      await e.wait((l) => l === 'readyok');
      const readyMs = performance.now() - t0;
      const t1 = performance.now();
      e.send('stop');
      const best = await e.wait((l) => l.startsWith('bestmove'));
      const stopMs = performance.now() - t1;
      // 정지 후에도 포지션·옵션이 유지되어 이어서 탐색할 수 있다(MultiPV 2 라인)
      e.lines.length = 0;
      e.send('go depth 4');
      await e.wait((l) => l.startsWith('bestmove'));
      return { best, stopMs, readyMs, multipv: e.lines.filter((l) => / multipv 2 /.test(l)).length };
    });
    check(r2.readyMs < 100, `탐색 중 isready 즉답: ${r2.readyMs.toFixed(0)} ms`);
    check(/^bestmove /.test(r2.best) && r2.stopMs < 1000, `stop → ${r2.best} in ${r2.stopMs.toFixed(0)} ms`);
    check(r2.multipv > 0, `정지 후 옵션(MultiPV 2)과 포지션이 복원됨 (${r2.multipv} lines)`);

    // 아직 깊이 1도 못 끝낸 순간의 stop 에도 반드시 수를 돌려준다
    const r3 = await pa.evaluate(async () => {
      const e = window.uci(new Worker('albatross.js'));
      e.send('uci'); await e.wait((l) => l === 'uciok');
      e.send('position startpos'); e.send('go infinite'); e.send('stop');
      return await e.wait((l) => l.startsWith('bestmove'));
    });
    check(/^bestmove [a-h][1-8][a-h][1-8]/.test(r3), `즉시 stop 에도 합법 수 반환: ${r3}`);

    // 여러 판을 이어서: ucinewgame, 연속 go
    const r4 = await pa.evaluate(async () => {
      const e = window.uci(new Worker('albatross.js'));
      e.send('uci'); await e.wait((l) => l === 'uciok');
      const moves = [];
      for (let i = 0; i < 6; i++) {
        e.lines.length = 0;
        e.send('position startpos' + (moves.length ? ' moves ' + moves.join(' ') : '')); e.send('go movetime 80');
        const b = await e.wait((l) => l.startsWith('bestmove'));
        moves.push(b.split(' ')[1]);
      }
      return moves;
    });
    check(r4.length === 6 && new Set(r4).size === 6, `6수 연속: ${r4.join(' ')}`);
    check(errs.length === 0, `페이지 오류 없음 ${errs.join('; ')}`);
    await ctxA.close();

    // ── 다른 사이트(다른 출처): 로더로 엔진을 가져다 쓴다 ──
    console.log('another site (different origin) via the loader');
    const ctxB = await browser.newContext(); const pb = await ctxB.newPage();
    const errsB = []; pb.on('pageerror', (e) => errsB.push(e.message)); pb.on('console', (m) => { if (m.type() === 'error') errsB.push(m.text()); });
    await pb.goto(`http://localhost:${B}/`);
    await pb.evaluate(helper);
    const direct = await pb.evaluate((url) => { try { new Worker(url); return 'allowed'; } catch (e) { return e.name; } }, `http://localhost:${A}/albatross.js`);
    check(direct === 'SecurityError', `다른 출처의 Worker 직접 생성은 브라우저가 막는다(${direct}) → 로더가 필요한 이유`);
    const r5 = await pb.evaluate(async () => {
      const worker = await createAlbatross();           // 로더: albatross.js 를 가져와 blob: 워커로 시작
      const e = window.uci(worker);
      e.send('uci'); await e.wait((l) => l === 'uciok');
      e.send('position fen kbK5/pp6/1P6/8/8/8/8/R7 w - - 0 1'); e.send('go depth 5');
      const best = await e.wait((l) => l.startsWith('bestmove'));
      const mate = e.lines.some((l) => / score mate 2 /.test(l));
      // blob: 로 시작해도 stop(워커 재시작)이 동작한다
      e.lines.length = 0;
      e.send('position startpos'); e.send('go infinite');
      await e.wait((l) => l.startsWith('info depth 3 '));
      e.send('stop');
      const stopped = await e.wait((l) => l.startsWith('bestmove'));
      return { best, mate, stopped, origin: location.origin };
    });
    check(r5.origin.endsWith(`:${B}`), `페이지 출처 ${r5.origin} (엔진 호스트 :${A} 와 다름)`);
    check(r5.best === 'bestmove a1a6' && r5.mate, `로더로 받은 엔진이 메이트 2수를 찾음: ${r5.best}`);
    check(/^bestmove /.test(r5.stopped), `blob 워커에서도 stop 동작: ${r5.stopped}`);
    check(errsB.length === 0, `다른 사이트 페이지 오류 없음 ${errsB.join('; ')}`);
    await ctxB.close();
  } finally {
    await browser.close(); sa.close(); sb.close();
  }
  console.log(failures ? `\n${failures} FAILED` : '\nall passed');
  process.exit(failures ? 1 : 0);
})().catch((e) => { console.error('E2E FAIL', e); process.exit(1); });
