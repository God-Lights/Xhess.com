#pragma once

#include <cstdint>
#include <iosfwd>

#include "position.h"

namespace albatross {

// Number of leaf nodes at the given depth (bulk-counts at depth 1).
std::uint64_t perft(Position& pos, int depth);

// Like perft, but prints "<move>: <nodes>" for every root move when out != nullptr.
std::uint64_t perft_divide(Position& pos, int depth, std::ostream* out);

}  // namespace albatross
