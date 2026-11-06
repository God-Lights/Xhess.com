#pragma once
// Transposition table: remembers the result of earlier searches of the same position (by Zobrist key).
// One entry per slot, replaced by depth and age. Used from a single search thread.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "../core/move.h"
#include "../core/types.h"

namespace albatross {

enum Bound : std::uint8_t { BOUND_NONE = 0, BOUND_UPPER = 1, BOUND_LOWER = 2, BOUND_EXACT = 3 };

struct TTEntry {
    std::uint32_t key32 = 0;   // upper half of the Zobrist key (the slot index uses the other bits)
    std::uint16_t move = 0;    // raw Move
    std::int16_t score = 0;    // mate scores are stored relative to the node (see to_tt / from_tt)
    std::int8_t depth = 0;
    std::uint8_t genBound = 0; // generation (upper 6 bits) | bound (lower 2 bits)

    Bound bound() const { return Bound(genBound & 3); }
    std::uint8_t generation() const { return genBound >> 2; }
};

struct TTProbe {
    bool found = false;
    Move move;
    int score = 0;  // already converted to "from the root" mate distance
    int depth = 0;
    Bound bound = BOUND_NONE;
};

class TranspositionTable {
public:
    explicit TranspositionTable(std::size_t megabytes = 16) { resize(megabytes); }

    void resize(std::size_t megabytes);  // discards the contents
    void clear();
    void new_search() { generation_ = std::uint8_t((generation_ + 1) & 63); }

    TTProbe probe(Key key, int ply) const;
    void store(Key key, int depth, int score, Bound bound, Move move, int ply);

    // Per mille of the first 1000 slots in use during the current generation (UCI "hashfull").
    int hashfull() const;
    std::size_t size() const { return table_.size(); }

private:
    std::size_t index(Key key) const { return std::size_t(key & mask_); }

    std::vector<TTEntry> table_;
    std::size_t mask_ = 0;
    std::uint8_t generation_ = 0;
};

}  // namespace albatross
