# Third-party notices

## Stockfish (bundled in the desktop app)

The Windows and Linux desktop installers include an unmodified **Stockfish 18** binary
(official release `sf_18`, `x86-64-avx2` build), licensed under the **GNU GPL v3**.

- License text: shipped with the app (`licenses/STOCKFISH-COPYING.txt`) and in the Stockfish repository
- Source code: https://github.com/official-stockfish/Stockfish/tree/sf_18
- Binary origin and integrity: `desktop/scripts/fetch-stockfish.mjs` downloads the official release archive
  and verifies its SHA-256 before it is bundled.

The browser/mobile build uses the Stockfish WebAssembly port from the `stockfish` npm package (GPL-3.0).
Both will be replaced by the in-house Albatross engine once it is ready.
