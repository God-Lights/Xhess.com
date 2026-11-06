#pragma once

#include <cstdint>

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
};

}  // namespace albatross
