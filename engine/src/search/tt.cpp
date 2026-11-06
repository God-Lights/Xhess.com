#include "tt.h"

#include <algorithm>

#include "search.h"

namespace albatross {

namespace {
// Mate scores depend on the distance from the root; stored scores are made relative to the node itself so that
// the same position reached at a different ply still gives a correct "mate in N".
int to_tt(int score, int ply) {
    if (score >= MATE_IN_MAX_PLY) return score + ply;
    if (score <= -MATE_IN_MAX_PLY) return score - ply;
    return score;
}
int from_tt(int score, int ply) {
    if (score >= MATE_IN_MAX_PLY) return score - ply;
    if (score <= -MATE_IN_MAX_PLY) return score + ply;
    return score;
}
}  // namespace

void TranspositionTable::resize(std::size_t megabytes) {
    std::size_t bytes = std::max<std::size_t>(1, megabytes) * 1024 * 1024;
    std::size_t slots = 1;
    while (slots * 2 * sizeof(TTEntry) <= bytes) slots *= 2;  // power of two: index by mask
    table_.assign(slots, TTEntry{});
    mask_ = slots - 1;
    generation_ = 0;
}

void TranspositionTable::clear() {
    std::fill(table_.begin(), table_.end(), TTEntry{});
    generation_ = 0;
}

TTProbe TranspositionTable::probe(Key key, int ply) const {
    const TTEntry& e = table_[index(key)];
    TTProbe r;
    if (e.bound() == BOUND_NONE || e.key32 != std::uint32_t(key >> 32)) return r;
    r.found = true;
    Move m;
    r.move = e.move ? Move(Square(e.move & 63), Square((e.move >> 6) & 63), MoveFlag(e.move >> 12)) : m;
    r.score = from_tt(e.score, ply);
    r.depth = e.depth;
    r.bound = e.bound();
    return r;
}

void TranspositionTable::store(Key key, int depth, int score, Bound bound, Move move, int ply) {
    TTEntry& e = table_[index(key)];
    const std::uint32_t k32 = std::uint32_t(key >> 32);
    const bool sameKey = e.key32 == k32 && e.bound() != BOUND_NONE;

    // Keep a deeper result from the current search for the same position; otherwise replace.
    // An older generation or a shallow entry is always replaceable.
    if (sameKey && e.generation() == generation_ && e.depth > depth + 2 && bound != BOUND_EXACT) {
        if (move.raw() && !e.move) e.move = move.raw();
        return;
    }
    if (!sameKey && e.bound() != BOUND_NONE && e.generation() == generation_ && e.depth > depth + 4) return;

    // Keep the previous best move when the new result has none (fail-low nodes).
    const std::uint16_t keepMove = move.raw() ? move.raw() : (sameKey ? e.move : std::uint16_t(0));
    e.key32 = k32;
    e.move = keepMove;
    e.score = std::int16_t(to_tt(score, ply));
    e.depth = std::int8_t(std::clamp(depth, -127, 127));
    e.genBound = std::uint8_t((generation_ << 2) | bound);
}

int TranspositionTable::hashfull() const {
    const std::size_t n = std::min<std::size_t>(1000, table_.size());
    int used = 0;
    for (std::size_t i = 0; i < n; ++i)
        if (table_[i].bound() != BOUND_NONE && table_[i].generation() == generation_) ++used;
    return int(used * 1000 / n);
}

}  // namespace albatross
