/*!
 * albatross-loader.js — use the Albatross chess engine on any website, without hosting the engine file yourself.
 *
 * Browsers do not allow `new Worker(url)` for a script on another origin. This helper downloads the engine
 * (a CORS-enabled single file) and starts it from a blob: URL instead.
 *
 *   <script src="https://YOUR-ALBATROSS-HOST/engine/albatross-loader.js"></script>
 *   <script>
 *     createAlbatross().then(function (engine) {          // engine is a Web Worker speaking UCI
 *       engine.onmessage = function (e) { console.log(e.data); };
 *       engine.postMessage('uci');
 *       engine.postMessage('position startpos');
 *       engine.postMessage('go movetime 1000');
 *     });
 *   </script>
 *
 * createAlbatross(url?) looks for albatross.js next to this loader unless you pass a url.
 * Pages with a strict Content-Security-Policy need `worker-src blob:` and permission to fetch the engine host.
 */
(function (root) {
  'use strict';
  var here = (document.currentScript && document.currentScript.src) || '';
  var defaultUrl = here ? here.replace(/[^/]*$/, 'albatross.js') : 'albatross.js';

  root.createAlbatross = function createAlbatross(url) {
    return fetch(url || defaultUrl)
      .then(function (res) {
        if (!res.ok) throw new Error('Albatross download failed: HTTP ' + res.status);
        return res.text();
      })
      .then(function (code) {
        var blob = new Blob([code], { type: 'text/javascript' });
        return new Worker(URL.createObjectURL(blob));
      });
  };
})(typeof self !== 'undefined' ? self : this);
