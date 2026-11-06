#pragma once
// Attack tables. Leapers use plain lookup tables; sliders use "fancy" magic bitboards whose magic
// numbers are found at start-up with a fixed-seed PRNG (deterministic, a few milliseconds).

#include "bitboard.h"

namespace albatross {

namespace Attacks {

struct Magic {
    Bitboard mask;
    Bitboard magic;
    Bitboard* attacks;
    unsigned shift;

    unsigned index(Bitboard occupied) const { return unsigned(((occupied & mask) * magic) >> shift); }
};

extern Bitboard PawnAttacks[COLOR_NB][SQUARE_NB];
extern Bitboard KnightAttacks[SQUARE_NB];
extern Bitboard KingAttacks[SQUARE_NB];
extern Magic RookMagics[SQUARE_NB];
extern Magic BishopMagics[SQUARE_NB];

void init();

}  // namespace Attacks

inline Bitboard pawn_attacks(Color c, Square s) { return Attacks::PawnAttacks[c][s]; }
inline Bitboard knight_attacks(Square s) { return Attacks::KnightAttacks[s]; }
inline Bitboard king_attacks(Square s) { return Attacks::KingAttacks[s]; }

inline Bitboard rook_attacks(Square s, Bitboard occupied) {
    const Attacks::Magic& m = Attacks::RookMagics[s];
    return m.attacks[m.index(occupied)];
}
inline Bitboard bishop_attacks(Square s, Bitboard occupied) {
    const Attacks::Magic& m = Attacks::BishopMagics[s];
    return m.attacks[m.index(occupied)];
}
inline Bitboard queen_attacks(Square s, Bitboard occupied) {
    return rook_attacks(s, occupied) | bishop_attacks(s, occupied);
}

// Attacks of a non-pawn piece type.
inline Bitboard attacks_bb(PieceType pt, Square s, Bitboard occupied) {
    switch (pt) {
        case KNIGHT: return knight_attacks(s);
        case BISHOP: return bishop_attacks(s, occupied);
        case ROOK:   return rook_attacks(s, occupied);
        case QUEEN:  return queen_attacks(s, occupied);
        case KING:   return king_attacks(s);
        default:     return 0;
    }
}

}  // namespace albatross
