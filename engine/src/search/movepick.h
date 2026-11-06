#pragma once

#include "../core/move.h"
#include "../core/position.h"

namespace albatross {

// Move-ordering score: higher is searched first. Captures by MVV-LVA (most valuable victim,
// least valuable attacker), then promotions, then everything else. Killer/history heuristics
// and TT moves are added with the 0.3 search work.
int score_move(const Position& pos, Move m);

}  // namespace albatross
