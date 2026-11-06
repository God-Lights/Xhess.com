#!/usr/bin/env python3
"""Engine-vs-engine match for Albatross versions (UCI, standard library only).

Plays two executables against each other from a set of short openings, each opening twice with colors swapped,
and reports W/D/L, score, Elo difference with a 95% interval, and LOS. Games are judged with the engine's own
rules through the non-standard `status` command (perft-verified move generation, threefold repetition,
50-move rule, insufficient material); games longer than --max-plies are drawn.

  tools/match.py --a build/albatross --b ../old/albatross-0.2 --games 200 --movetime 50 --concurrency 4

Elo is the Elo of A relative to B. It is only meaningful at the chosen time control and with enough games:
the interval shows how much (or how little) the result can be trusted.
"""
import argparse, math, subprocess, sys, time
from concurrent.futures import ThreadPoolExecutor

OPENINGS = [
    "e2e4 e7e5 g1f3 b8c6", "e2e4 c7c5 g1f3 d7d6", "d2d4 d7d5 c2c4 e7e6", "d2d4 g8f6 c2c4 g7g6",
    "e2e4 e7e6 d2d4 d7d5", "c2c4 e7e5 b1c3 g8f6", "g1f3 d7d5 g2g3 g8f6", "e2e4 c7c6 d2d4 d7d5",
    "d2d4 g8f6 g1f3 e7e6", "e2e4 d7d6 d2d4 g8f6", "e2e4 g7g6 d2d4 f8g7", "c2c4 c7c5 g1f3 g8f6",
    "e2e4 e7e5 g1f3 g8f6", "d2d4 d7d5 g1f3 g8f6", "e2e4 c7c5 b1c3 b8c6", "d2d4 f7f5 g2g3 g8f6",
]


class Engine:
    def __init__(self, exe, hash_mb):
        self.p = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.DEVNULL, text=True, bufsize=1)
        self.send("uci")
        self.read_until(lambda l: l == "uciok")
        self.send(f"setoption name Hash value {hash_mb}")
        self.send("isready")
        self.read_until(lambda l: l == "readyok")

    def send(self, s):
        self.p.stdin.write(s + "\n")
        self.p.stdin.flush()

    def read_until(self, pred):
        while True:
            line = self.p.stdout.readline()
            if line == "":
                raise RuntimeError("engine exited")
            line = line.strip()
            if pred(line):
                return line

    def bestmove(self, moves, movetime):
        self.send("position startpos" + (" moves " + " ".join(moves) if moves else ""))
        self.send(f"go movetime {movetime}")
        return self.read_until(lambda l: l.startswith("bestmove")).split()[1]

    def status(self, moves):
        self.send("position startpos" + (" moves " + " ".join(moves) if moves else ""))
        self.send("status")
        return self.read_until(lambda l: l.startswith("status ")).split()[1]

    def close(self):
        try:
            self.send("quit")
            self.p.wait(timeout=2)
        except Exception:
            self.p.kill()


def play(exe_white, exe_black, opening, movetime, hash_mb, max_plies, oracle_exe):
    """Returns (result for white: 1 / 0.5 / 0, termination)."""
    w, b, oracle = Engine(exe_white, hash_mb), Engine(exe_black, hash_mb), Engine(oracle_exe, 1)
    moves = opening.split()
    try:
        for ply in range(len(moves), max_plies):
            st = oracle.status(moves)
            if st == "checkmate":
                return (0.0 if ply % 2 == 0 else 1.0), st      # side to move is mated
            if st != "ongoing":
                return 0.5, st
            eng = w if ply % 2 == 0 else b
            mv = eng.bestmove(moves, movetime)
            if mv == "0000":
                return 0.5, "no-move"
            moves.append(mv)
        return 0.5, "max-plies"
    except Exception as e:          # a crash or a hang loses the game for the side that was to move
        return (0.0 if len(moves) % 2 == 0 else 1.0), f"crash: {e}"
    finally:
        for e in (w, b, oracle):
            e.close()


def elo(score):
    score = min(max(score, 1e-9), 1 - 1e-9)
    return -400 * math.log10(1 / score - 1)


def report(a, b, w, d, l):
    n = w + d + l
    s = (w + 0.5 * d) / n
    var = (w * (1 - s) ** 2 + d * (0.5 - s) ** 2 + l * s ** 2) / n
    se = math.sqrt(var / n)
    lo, hi = elo(s - 1.96 * se), elo(s + 1.96 * se)
    los = 0.5 * (1 + math.erf((w - l) / math.sqrt(2 * (w + l)))) if w + l else 0.5
    print(f"\n{a}  vs  {b}")
    print(f"games {n}: +{w} ={d} -{l}   score {100 * s:.1f}%")
    print(f"Elo (A - B): {elo(s):+.0f}   95% interval [{lo:+.0f}, {hi:+.0f}]   LOS {100 * los:.1f}%")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--a", required=True, help="engine under test")
    ap.add_argument("--b", required=True, help="reference engine")
    ap.add_argument("--games", type=int, default=100)
    ap.add_argument("--movetime", type=int, default=50, help="ms per move")
    ap.add_argument("--hash", type=int, default=16)
    ap.add_argument("--concurrency", type=int, default=4)
    ap.add_argument("--max-plies", type=int, default=300)
    args = ap.parse_args()

    jobs = []
    for g in range(args.games):
        opening = OPENINGS[(g // 2) % len(OPENINGS)]
        a_white = g % 2 == 0
        jobs.append((g, opening, a_white))

    w = d = l = 0
    terms = {}
    t0 = time.time()

    def run(job):
        g, opening, a_white = job
        white, black = (args.a, args.b) if a_white else (args.b, args.a)
        res, term = play(white, black, opening, args.movetime, args.hash, args.max_plies, args.a)
        return g, (res if a_white else 1 - res), term

    with ThreadPoolExecutor(max_workers=args.concurrency) as pool:
        for g, res, term in pool.map(run, jobs):
            if res == 1: w += 1
            elif res == 0: l += 1
            else: d += 1
            terms[term] = terms.get(term, 0) + 1
            if (g + 1) % 10 == 0:
                print(f"  {g + 1}/{args.games}: +{w} ={d} -{l}", flush=True)
    report(args.a, args.b, w, d, l)
    print("terminations:", dict(sorted(terms.items())), f"| {time.time() - t0:.0f}s")


if __name__ == "__main__":
    sys.exit(main())
