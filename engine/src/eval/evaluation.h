#pragma once
// Static evaluation. Search only depends on this interface, so a different evaluator (NNUE later)
// can replace classical.cpp without touching the search.

#include "../core/position.h"

namespace albatross {

// Centipawns, indexed by PieceType. The king has no material value.
constexpr int PieceValue[PIECE_TYPE_NB] = {0, 100, 320, 330, 500, 900, 0};

// Score in centipawns from the point of view of the side to move.
int evaluate(const Position& pos);

}  // namespace albatross
