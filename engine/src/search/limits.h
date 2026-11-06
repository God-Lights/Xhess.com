#pragma once

#include <cstdint>
#include <vector>

#include "../core/move.h"

namespace albatross {

// Search limits from the UCI "go" command. Zero means "not set".
struct Limits {
    int depth = 0;
    std::uint64_t nodes = 0;
    std::int64_t movetime = 0;  // ms
    std::int64_t time[2] = {0, 0};  // remaining time per color (wtime, btime), ms
    std::int64_t inc[2] = {0, 0};   // increment per color, ms
    int movestogo = 0;
    bool infinite = false;
    std::vector<Move> searchMoves;  // "go searchmoves ...": only these root moves are considered (empty = all)
    int multiPv = 1;                // UCI option MultiPV, copied here by the UCI layer
};

}  // namespace albatross
