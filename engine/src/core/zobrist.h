#pragma once

#include "types.h"

namespace albatross::Zobrist {

extern Key psq[PIECE_NB][SQUARE_NB];
extern Key castling[CASTLING_RIGHT_NB];  // indexed by the full rights mask; castling[0] == 0
extern Key enpassant[8];                 // by file; only mixed in when an en passant capture is possible
extern Key side;                         // mixed in when Black is to move

void init();

}  // namespace albatross::Zobrist
