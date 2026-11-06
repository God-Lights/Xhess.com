#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

#include "../core/move.h"
#include "../core/position.h"
#include "limits.h"
#include "tt.h"

namespace albatross {

constexpr int MAX_PLY = 128;
constexpr int MATE = 32000;
constexpr int MATE_IN_MAX_PLY = MATE - MAX_PLY;  // scores at or above this are forced mates
constexpr int INFINITE_SCORE = 32001;

constexpr bool is_mate_score(int score) { return score >= MATE_IN_MAX_PLY || score <= -MATE_IN_MAX_PLY; }

// Progress report after every completed iteration.
struct SearchInfo {
    int multipv = 1;  // 1-based index of the line (MultiPV)
    int depth = 0;
    int seldepth = 0;
    int score = 0;  // centipawns or mate score, from the side to move
    std::uint64_t nodes = 0;
    std::int64_t timeMs = 0;
    int hashfull = 0;  // per mille
    std::vector<Move> pv;
};

// One principal variation of the root position (MultiPV: lines are ordered best first).
struct RootLine {
    int score = 0;
    std::vector<Move> pv;
};

struct SearchResult {
    Move best;  // Move::none() only if the root position has no legal move
    int score = 0;
    int depth = 0;  // last completed iteration
    std::uint64_t nodes = 0;
    std::vector<Move> pv;
    std::vector<RootLine> lines;  // all MultiPV lines of the last completed iteration (lines[0] is best/pv)
};

using InfoCallback = std::function<void(const SearchInfo&)>;
using BestMoveCallback = std::function<void(const SearchResult&)>;

// Synchronous alpha-beta search with iterative deepening. `stop` can be raised from another thread.
// The transposition table is shared between searches by the caller.
SearchResult search_position(Position pos, const Limits& limits, std::atomic<bool>& stop, TranspositionTable& tt,
                             const InfoCallback& onInfo = {});

// Convenience overload with a private 8 MB table (tests, one-off searches).
SearchResult search_position(Position pos, const Limits& limits, std::atomic<bool>& stop,
                             const InfoCallback& onInfo = {});

// Runs a search on its own thread so the UCI loop stays responsive ("stop", "isready").
class Search {
public:
    Search() = default;
    ~Search();
    Search(const Search&) = delete;
    Search& operator=(const Search&) = delete;

    void go(Position pos, const Limits& limits, InfoCallback onInfo, BestMoveCallback onBest);
    void stop();  // ask the running search to finish; the bestmove callback still fires
    void wait();  // block until the search thread has finished
    bool infinite() const { return infinite_; }

    // Only call while no search is running.
    void set_hash_mb(std::size_t megabytes) { tt_.resize(megabytes); }
    void clear_hash() { tt_.clear(); }
    TranspositionTable& tt() { return tt_; }

private:
    TranspositionTable tt_{16};
    std::atomic<bool> stop_{false};
    std::thread thread_;
    bool infinite_ = false;
};

}  // namespace albatross
