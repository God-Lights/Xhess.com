#include "see.h"

#include <algorithm>

#include "../eval/evaluation.h"

namespace albatross {

namespace {

// Least valuable piece of `side` among `attackers`; returns its square bitboard (0 if none).
Bitboard least_valuable(const Position& pos, Bitboard attackers, Color side, PieceType& pt) {
    for (int t = PAWN; t <= KING; ++t) {
        const Bitboard b = attackers & pos.pieces(side, PieceType(t));
        if (b) {
            pt = PieceType(t);
            return b & (~b + 1);  // lowest set bit
        }
    }
    return 0;
}

// The king may only recapture if the square is no longer defended; model that by a very large value.
constexpr int KingValue = 20000;
int value_of(PieceType pt) { return pt == KING ? KingValue : PieceValue[pt]; }

}  // namespace

int see(const Position& pos, Move m) {
    if (!m.is_capture() || m.is_castling()) return 0;

    const Square from = m.from(), to = m.to();
    const Color us = color_of(pos.piece_on(from));

    PieceType attacker = type_of(pos.piece_on(from));
    Bitboard occ = pos.pieces() ^ square_bb(from);

    int gain[40];
    int d = 0;
    if (m.flag() == EP_CAPTURE) {
        occ ^= square_bb(to - pawn_push(us));  // the captured pawn is not on the destination square
        gain[0] = PieceValue[PAWN];
    } else {
        gain[0] = PieceValue[type_of(pos.piece_on(to))];
    }

    // All pieces that attack `to` given the current occupancy; x-ray attackers are added as pieces leave.
    Bitboard attackers = pos.attackers_to(to, occ) & occ;
    // The capturing piece itself stands on `to` now and is not an attacker of its own square.
    Color side = ~us;

    for (;;) {
        ++d;
        PieceType next = NO_PIECE_TYPE;
        const Bitboard fromBb = least_valuable(pos, attackers & pos.pieces(side), side, next);
        if (!fromBb) break;

        gain[d] = value_of(attacker) - gain[d - 1];  // speculative: the previous capturer is captured now
        if (std::max(-gain[d - 1], gain[d]) < 0) break;  // neither side can improve: prune

        occ ^= fromBb;
        attackers ^= fromBb;
        // Removing a piece can open lines for sliders behind it.
        attackers |= (bishop_attacks(to, occ) & pos.pieces(BISHOP, QUEEN)) & occ;
        attackers |= (rook_attacks(to, occ) & pos.pieces(ROOK, QUEEN)) & occ;

        attacker = next;
        side = ~side;
        if (d >= 38) break;
    }

    while (--d > 0) gain[d - 1] = -std::max(-gain[d - 1], gain[d]);
    return gain[0];
}

}  // namespace albatross
