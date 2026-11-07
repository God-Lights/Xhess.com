// ---- Albatross worker driver (appended to the Emscripten glue by wasm/build.sh) ----
//
// The same file runs in two roles inside Web Workers:
//   * "supervisor" (the worker the page creates): speaks UCI with the page, owns a "core" worker and answers
//     isready / stop even while the core is busy searching.
//   * "core" (name 'albatross-core', created by the supervisor): hosts the WebAssembly engine. A search blocks
//     it, so it cannot hear "stop"; the supervisor stops a search by terminating the core and starting a fresh
//     one (the transposition table is lost, the position and options are replayed).
// Outside a worker (Node tests) nothing happens and the module factory is used directly.
(function () {
  'use strict';
  var inWorker = typeof WorkerGlobalScope !== 'undefined' && self instanceof WorkerGlobalScope;
  if (!inWorker) return;
  if (self.name === 'albatross-core') startCore();
  else startSupervisor();

  function startCore() {
    var mod = null;
    var pending = [];
    function feed(data) {
      String(data).split('\n').forEach(function (raw) {
        var line = raw.trim();
        if (line) mod.ccall('albatross_command', null, ['string'], [line]);
      });
    }
    self.onmessage = function (e) {
      if (mod) feed(e.data);
      else pending.push(e.data);
    };
    AlbatrossModule({
      onLine: function (line) { postMessage(line); },
    }).then(function (m) {
      mod = m;
      mod.ccall('albatross_init', null, [], []);
      pending.splice(0).forEach(feed);
      postMessage({ albatrossReady: true });
    }, function (err) {
      postMessage('info string albatross: failed to start the WebAssembly engine: ' + err);
    });
  }

  function startSupervisor() {
    var selfUrl = self.location.href;   // works for http(s): and blob: URLs alike
    var core = null;
    var ready = false;
    var queue = [];
    var options = Object.create(null);  // option name -> its latest "setoption" line (replayed after a restart)
    var positionCmd = null;
    var searching = false;
    var lastBest = null;                // first move of the latest completed iteration of the running search

    function spawn() {
      ready = false;
      core = new Worker(selfUrl, { name: 'albatross-core' });
      core.onmessage = onCore;
      core.onerror = function (e) { postMessage('info string albatross: engine worker error: ' + (e && e.message)); };
    }

    function send(line) {
      if (ready) core.postMessage(line);
      else queue.push(line);
    }

    function onCore(e) {
      var d = e.data;
      if (typeof d !== 'string') {
        if (d && d.albatrossReady) {
          ready = true;
          queue.splice(0).forEach(function (l) { core.postMessage(l); });
        }
        return;
      }
      if (d.indexOf('info ') === 0 && d.indexOf(' pv ') > 0) {
        var mp = /\bmultipv (\d+)/.exec(d);
        if (!mp || mp[1] === '1') lastBest = /\bpv (\S+)/.exec(d)[1];
      } else if (d.indexOf('bestmove') === 0) {
        searching = false;
        lastBest = null;
      }
      postMessage(d);
    }

    // Stop the running search: restart the core and answer with the best move found so far.
    function stopSearch() {
      var best = lastBest;
      core.terminate();
      spawn();
      Object.keys(options).forEach(function (name) { send(options[name]); });
      if (positionCmd) send(positionCmd);
      if (best) {
        searching = false;
        lastBest = null;
        postMessage('bestmove ' + best);
      } else {
        // Not even depth 1 was finished: ask the fresh engine for a quick move so the page always gets one.
        send('go depth 1');
      }
    }

    spawn();

    self.onmessage = function (e) {
      String(e.data).split('\n').forEach(function (raw) {
        var line = raw.trim();
        if (!line) return;
        var cmd = line.split(/\s+/)[0];
        if (cmd === 'stop') {
          if (searching) stopSearch();
          return;
        }
        if (cmd === 'isready' && searching) { postMessage('readyok'); return; }
        if (cmd === 'quit') { core.terminate(); self.close(); return; }
        if (cmd === 'setoption') {
          var m = /^setoption\s+name\s+(.+?)(?:\s+value\s+.*)?$/i.exec(line);
          if (m) options[m[1]] = line;
        } else if (cmd === 'position') {
          positionCmd = line;
        } else if (cmd === 'ucinewgame') {
          positionCmd = null;
        } else if (cmd === 'go') {
          searching = true;
          lastBest = null;
        }
        send(line);
      });
    };
  }
})();
