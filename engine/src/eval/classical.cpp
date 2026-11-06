// Hand-crafted evaluation, version 0.2: material + piece-square tables.
// The king table is blended between middlegame and endgame by the amount of non-pawn material;
// the full tapered evaluation (all terms) arrives in 0.4.
//
// Tables are written from White's point of view with rank 1 first and file A first, which matches
// the LERF square numbering (A1 = 0). Black uses the vertically mirrored square (sq ^ 56).
// The values are hand-written starting points and will be tuned by self-play later.

#include "evaluation.h"

namespace albatross {

namespace {

// clang-format off
constexpr int PawnPst[64] = {
      0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0, -10, -10,   0,   0,   0,
      0,   0,   5,   5,   5,   5,   0,   0,
      0,   0,   5,  20,  20,   5,   0,   0,
      5,   5,  10,  25,  25,  10,   5,   5,
     10,  10,  20,  30,  30,  20,  10,  10,
     50,  50,  50,  50,  50,  50,  50,  50,
      0,   0,   0,   0,   0,   0,   0,   0};

constexpr int KnightPst[64] = {
    -50, -40, -30, -30, -30, -30, -40, -50,
    -40, -20,   0,   5,   5,   0, -20, -40,
    -30,   5,  10,  15,  15,  10,   5, -30,
    -30,   0,  15,  20,  20,  15,   0, -30,
    -30,   5,  15,  20,  20,  15,   5, -30,
    -30,   0,  10,  15,  15,  10,   0, -30,
    -40, -20,   0,   0,   0,   0, -20, -40,
    -50, -40, -30, -30, -30, -30, -40, -50};

constexpr int BishopPst[64] = {
    -20, -10, -10, -10, -10, -10, -10, -20,
    -10,   5,   0,   0,   0,   0,   5, -10,
    -10,  10,  10,  10,  10,  10,  10, -10,
    -10,   0,  10,  10,  10,  10,   0, -10,
    -10,   5,   5,  10,  10,   5,   5, -10,
    -10,   0,   5,  10,  10,   5,   0, -10,
    -10,   0,   0,   0,   0,   0,   0, -10,
    -20, -10, -10, -10, -10, -10, -10, -20};

constexpr int RookPst[64] = {
      0,   0,   0,   5,   5,   0,   0,   0,
     -5,   0,   0,   0,   0,   0,   0,  -5,
     -5,   0,   0,   0,   0,   0,   0,  -5,
     -5,   0,   0,   0,   0,   0,   0,  -5,
     -5,   0,   0,   0,   0,   0,   0,  -5,
     -5,   0,   0,   0,   0,   0,   0,  -5,
      5,  10,  10,  10,  10,  10,  10,   5,
      0,   0,   0,   0,   0,   0,   0,   0};

constexpr int QueenPst[64] = {
    -20, -10, -10,  -5,  -5, -10, -10, -20,
    -10,   0,   5,   0,   0,   0,   0, -10,
    -10,   5,   5,   5,   5,   5,   0, -10,
      0,   0,   5,   5,   5,   5,   0,  -5,
     -5,   0,   5,   5,   5,   5,   0,  -5,
    -10,   0,   5,   5,   5,   5,   0, -10,
    -10,   0,   0,   0,   0,   0,   0, -10,
    -20, -10, -10,  -5,  -5, -10, -10, -20};

constexpr int KingMiddlegamePst[64] = {
     20,  30,  10,   0,   0,  10,  30,  20,
     20,  20,   0,   0,   0,   0,  20,  20,
    -10, -20, -20, -20, -20, -20, -20, -10,
    -20, -30, -30, -40, -40, -30, -30, -20,
    -30, -40, -40, -50, -50, -40, -40, -30,
    -30, -40, -40, -50, -50, -40, -40, -30,
    -30, -40, -40, -50, -50, -40, -40, -30,
    -30, -40, -40, -50, -50, -40, -40, -30};

constexpr int KingEndgamePst[64] = {
    -50, -30, -30, -30, -30, -30, -30, -50,
    -30, -30,   0,   0,   0,   0, -30, -30,
    -30, -10,  20,  30,  30,  20, -10, -30,
    -30, -10,  30,  40,  40,  30, -10, -30,
    -30, -10,  30,  40,  40,  30, -10, -30,
    -30, -10,  20,  30,  30,  20, -10, -30,
    -30, -30,   0,   0,   0,   0, -30, -30,
    -50, -30, -30, -30, -30, -30, -30, -50};
// clang-format on

constexpr const int* Pst[PIECE_TYPE_NB] = {nullptr, PawnPst, KnightPst, BishopPst, RookPst, QueenPst, nullptr};

// Game phase weights: 24 at the starting position, 0 with only kings and pawns.
constexpr int PhaseWeight[PIECE_TYPE_NB] = {0, 0, 1, 1, 2, 4, 0};
constexpr int MaxPhase = 24;

}  // namespace

int evaluate(const Position& pos) {
    int score[COLOR_NB] = {0, 0};
    int phase = 0;

    for (Color c : {WHITE, BLACK}) {
        const int flip = c == WHITE ? 0 : 56;
        for (int t = PAWN; t <= QUEEN; ++t) {
            const PieceType pt = PieceType(t);
            for (Bitboard b = pos.pieces(c, pt); b;) {
                const Square s = pop_lsb(b);
                score[c] += PieceValue[pt] + Pst[pt][s ^ flip];
                phase += PhaseWeight[pt];
            }
        }
    }
    if (phase > MaxPhase) phase = MaxPhase;  // promotions can push the sum above the starting value

    int king[COLOR_NB];
    for (Color c : {WHITE, BLACK}) {
        const int sq = int(pos.king_square(c)) ^ (c == WHITE ? 0 : 56);
        king[c] = (KingMiddlegamePst[sq] * phase + KingEndgamePst[sq] * (MaxPhase - phase)) / MaxPhase;
    }

    const int white = score[WHITE] + king[WHITE];
    const int black = score[BLACK] + king[BLACK];
    return pos.side_to_move() == WHITE ? white - black : black - white;
}

}  // namespace albatross
