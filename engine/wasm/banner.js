/*!
 * Albatross chess engine (WebAssembly build) — UCI engine for the browser, single file, no dependencies.
 * https://github.com/God-Lights/Xhess.com   License: MIT (see repository LICENSE)
 *
 * Use it as a Web Worker. The worker speaks UCI: send commands as strings, receive output lines as strings.
 *
 *   const engine = new Worker('albatross.js');
 *   engine.onmessage = (e) => console.log(e.data);          // "uciok", "info depth 8 ...", "bestmove e2e4"
 *   engine.postMessage('uci');
 *   engine.postMessage('position startpos moves e2e4 e7e5');
 *   engine.postMessage('go movetime 1000');
 *
 * To load it from another website without hosting the file yourself, see albatross-loader.js.
 * Notes: single-threaded; "stop" restarts the engine worker (the hash table is cleared) so it works even
 * though a WebAssembly search cannot be interrupted from inside.
 */
