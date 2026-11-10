// Node 에서 WebAssembly 모듈(워커 드라이버 없이)을 직접 검증한다.
//   node engine/wasm/tests/node_test.cjs [albatross.js] [native-albatross-binary]
// 네이티브 바이너리를 주면, 같은 고정 깊이 탐색의 노드 수가 정확히 같은지(= WebAssembly 빌드가 같은 결과를 내는지) 비교한다.
const path = require('path');
const { execFileSync } = require('child_process');
const file = path.resolve(process.argv[2] || path.join(__dirname, '../dist/albatross.js'));
const nativeBin = process.argv[3];
process.setMaxListeners(0);
const factory = require(file);

let failures = 0;
const check = (cond, msg) => { if (!cond) { failures++; console.error('  FAIL', msg); } else console.log('  ok  ', msg); };

async function engine() {
  const lines = [];
  const mod = await factory({ onLine: (l) => lines.push(l) });
  mod.ccall('albatross_init', null, [], []);
  return { lines, cmd: (c) => mod.ccall('albatross_command', null, ['string'], [c]) };
}

(async () => {
  // 1) 핸드셰이크와 옵션
  let e = await engine();
  e.cmd('uci'); e.cmd('isready');
  check(e.lines.some((l) => l.startsWith('id name Albatross 0.5')), 'id name Albatross 0.5');
  check(e.lines.includes('uciok') && e.lines.includes('readyok'), 'uciok / readyok');
  check(e.lines.some((l) => /option name Hash type spin default 16 min 1 max 256/.test(l)), 'Hash 최대 256 MB(브라우저 빌드 제한)');
  check(e.lines.some((l) => /option name MultiPV/.test(l)), 'MultiPV 옵션');

  // 2) Perft (이동 생성의 정확성)
  e = await engine();
  e.cmd('position startpos'); e.cmd('go perft 5');
  check(e.lines.some((l) => l === 'Nodes searched: 4865609'), 'perft(5) 시작 포지션 = 4,865,609');
  e = await engine();
  e.cmd('position fen r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1'); e.cmd('go perft 4');
  check(e.lines.some((l) => l === 'Nodes searched: 4085603'), 'perft(4) Kiwipete = 4,085,603');

  // 3) 탐색: 메이트 발견, MultiPV, searchmoves, 시간 제한
  e = await engine();
  e.cmd('position fen kbK5/pp6/1P6/8/8/8/8/R7 w - - 0 1'); e.cmd('go depth 5');
  check(e.lines.some((l) => /score mate 2 /.test(l)), '메이트 2수 발견');
  check(e.lines.at(-1) === 'bestmove a1a6', 'bestmove a1a6');
  e = await engine();
  e.cmd('setoption name MultiPV value 3'); e.cmd('position startpos'); e.cmd('go depth 4');
  check([1, 2, 3].every((k) => e.lines.some((l) => l.includes(` multipv ${k} `))), 'MultiPV 3 라인');
  e = await engine();
  e.cmd('position startpos'); e.cmd('go depth 3 searchmoves a2a3');
  check(e.lines.at(-1) === 'bestmove a2a3', 'searchmoves a2a3');
  e = await engine();
  const t0 = Date.now(); e.cmd('position startpos'); e.cmd('go movetime 300');
  const dt = Date.now() - t0;
  check(e.lines.at(-1).startsWith('bestmove') && dt < 900, `go movetime 300 → ${dt} ms`);

  // 4) eval 명령, 잘못된 입력
  e = await engine();
  e.cmd('position startpos'); e.cmd('eval');
  check(e.lines.some((l) => l.includes('Final evaluation: +0.00')), 'eval 명령');
  e.cmd('position fen garbage');
  check(e.lines.some((l) => l.includes('illegal fen')), '잘못된 FEN 거부');

  // 4b) 전략 테마(themes): 칸과 값, 종료 줄
  e = await engine();
  e.cmd('position fen k7/8/8/4P3/8/8/8/4K3 w - - 0 1'); e.cmd('themes');
  check(e.lines.some((l) => /^theme passed white e5 cp -?\d+ mg -?\d+ eg -?\d+$/.test(l)), 'themes: 통과폰 e5');
  check(e.lines.some((l) => l.startsWith('theme unstoppable white e5 cp 450 ')), 'themes: 막을 수 없는 통과폰 450');
  check(e.lines.at(-1).startsWith('themes done phase 0 scale 128 white '), 'themes: 종료 줄');

  // 5) 속도 (참고용)
  e = await engine();
  e.cmd('position fen r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10');
  const t1 = Date.now(); e.cmd('go depth 9');
  const info = e.lines.filter((l) => l.startsWith('info depth 9 ')).pop();
  const nodes = Number(/ nodes (\d+)/.exec(info)[1]);
  console.log(`  info  WASM depth 9: ${nodes} nodes in ${Date.now() - t1} ms (~${Math.round(nodes / (Date.now() - t1))} knps)`);

  // 6) 네이티브와 같은 결과인가 (고정 깊이 탐색은 결정적)
  if (nativeBin) {
    const fens = [
      'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1',
      'r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1',
      '8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1',
      'r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10',
    ];
    for (const fen of fens) {
      const w = await engine();
      w.cmd(`position fen ${fen}`); w.cmd('go depth 8');
      const wLast = w.lines.filter((l) => l.startsWith('info depth 8 ')).pop();
      const input = `position fen ${fen}\ngo depth 8\n`;
      const out = execFileSync(nativeBin, { input, encoding: 'utf8' });
      const nLast = out.split('\n').filter((l) => l.startsWith('info depth 8 ')).pop();
      const key = (l) => `${/ nodes (\d+)/.exec(l)[1]} ${/ score (\S+ -?\d+)/.exec(l)[1]} ${/ pv (.*)$/.exec(l)[1]}`;
      check(key(wLast) === key(nLast), `네이티브와 동일(노드·점수·수순): ${fen.slice(0, 28)}… → ${/ nodes (\d+)/.exec(wLast)[1]} nodes`);
    }
  }
  console.log(failures ? `\n${failures} FAILED` : '\nall passed');
  process.exit(failures ? 1 : 0);
})().catch((err) => { console.error(err); process.exit(1); });
