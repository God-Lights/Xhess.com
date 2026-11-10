#include "datagen.h"

#include <atomic>
#include <cstdlib>

#include "core/movegen.h"
#include "core/position.h"
#include "search/search.h"

namespace albatross::tools {

namespace {

struct Rng {
    std::uint64_t s;
    std::uint64_t next() {   // splitmix64
        std::uint64_t z = (s += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
    std::size_t below(std::size_t n) { return std::size_t(next() % n); }
};

// Is the game over by rule? Returns "" if not, otherwise the termination and sets `result`.
std::string game_over(const Position& pos, double& result) {
    MoveList moves;
    generate_legal(pos, moves);
    if (moves.empty()) {
        if (pos.in_check()) {
            result = pos.side_to_move() == WHITE ? 0.0 : 1.0;   // the side to move is mated
            return "checkmate";
        }
        result = 0.5;
        return "stalemate";
    }
    if (pos.insufficient_material() || pos.is_fifty_move_draw() || pos.repetition_count() >= 3) {
        result = 0.5;
        return "draw_rule";
    }
    return "";
}

}  // namespace

GameRecord play_game(std::uint64_t seed, const DatagenOptions& opt) {
    Rng rng{seed * 0x2545F4914F6CDD1DULL + 12345};
    TranspositionTable tt(8);
    std::atomic<bool> stop{false};
    Limits budget;
    budget.nodes = opt.nodes;

    Position pos;
    // A random opening: retry until it is playable and roughly balanced.
    for (int attempt = 0; attempt < 50; ++attempt) {
        pos.set(Position::START_FEN);
        bool ok = true;
        for (int i = 0; i < opt.openingPlies && ok; ++i) {
            MoveList moves;
            generate_legal(pos, moves);
            if (moves.empty()) { ok = false; break; }
            pos.do_move(moves[rng.below(moves.size())]);
        }
        double dummy;
        if (!ok || !game_over(pos, dummy).empty()) continue;
        Limits probe;
        probe.nodes = 2000;
        tt.clear();
        if (std::abs(search_position(pos, probe, stop, tt).score) <= opt.maxOpeningEval) break;
    }
    tt.clear();

    GameRecord rec;
    std::vector<std::string> fens;   // quiet positions kept, labelled with the result at the end
    int winStreak = 0, drawStreak = 0;
    int streakSign = 0;

    for (int ply = 0; ply < opt.maxPlies; ++ply) {
        double result = 0.5;
        const std::string over = game_over(pos, result);
        if (!over.empty()) {
            rec.result = result;
            rec.termination = over;
            break;
        }

        const SearchResult r = search_position(pos, budget, stop, tt);
        const int whiteScore = pos.side_to_move() == WHITE ? r.score : -r.score;

        const bool quiet = !pos.in_check() && !r.best.is_capture() && !r.best.is_promotion() && !is_mate_score(r.score) &&
                           std::abs(r.score) < 2000;
        if (quiet && rng.below(std::size_t(opt.sampleOneIn)) == 0) fens.push_back(pos.fen());

        // A forced mate has been found: the game is decided.
        if (is_mate_score(r.score)) {
            rec.result = whiteScore > 0 ? 1.0 : 0.0;
            rec.termination = "mate_found";
            break;
        }
        // Adjudication: a large advantage held for several plies wins, a dead-equal late position draws.
        const int sign = whiteScore >= 1200 ? 1 : (whiteScore <= -1200 ? -1 : 0);
        if (sign != 0 && sign == streakSign) ++winStreak;
        else winStreak = sign != 0 ? 1 : 0;
        streakSign = sign;
        if (winStreak >= 6) {
            rec.result = sign > 0 ? 1.0 : 0.0;
            rec.termination = "win_adjudication";
            break;
        }
        drawStreak = (ply >= 80 && std::abs(whiteScore) <= 8) ? drawStreak + 1 : 0;
        if (drawStreak >= 12) {
            rec.result = 0.5;
            rec.termination = "draw_adjudication";
            break;
        }

        pos.do_move(r.best);
        rec.plies = ply + 1;
        if (ply + 1 == opt.maxPlies) {
            rec.result = 0.5;
            rec.termination = "max_plies";
        }
    }
    if (rec.termination.empty()) rec.termination = "max_plies";
    for (const std::string& f : fens) rec.samples.push_back({f, rec.result});
    return rec;
}

}  // namespace albatross::tools
