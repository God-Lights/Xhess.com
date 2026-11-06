#include "movepick.h"

#include "../eval/evaluation.h"

namespace albatross {

int score_move(const Position& pos, Move m) {
    int s = 0;
    if (m.is_capture()) {
        const PieceType victim = m.flag() == EP_CAPTURE ? PAWN : type_of(pos.piece_on(m.to()));
        const PieceType attacker = type_of(pos.piece_on(m.from()));
        // The victim term moves in steps of at least 80, the attacker term stays below 60.
        s += 10000 + PieceValue[victim] * 8 - int(attacker) * 10;
    }
    if (m.is_promotion()) s += 9000 + PieceValue[m.promotion_type()];
    return s;
}

}  // namespace albatross
