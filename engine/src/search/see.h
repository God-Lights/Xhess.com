#pragma once

#include "../core/move.h"
#include "../core/position.h"

namespace albatross {

// Static exchange evaluation: material outcome (centipawns, from the mover's point of view) of the capture
// sequence on the destination square if both sides keep recapturing with their least valuable attacker
// and may stop at any time. Pins are ignored. Non-captures return 0; promotions and castling are not analysed.
int see(const Position& pos, Move m);

inline bool see_ge(const Position& pos, Move m, int threshold) { return see(pos, m) >= threshold; }

}  // namespace albatross
