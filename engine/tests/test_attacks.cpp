#include <cstdint>

#include "core/attacks.h"
#include "test.h"

using namespace albatross;

namespace {

// Independent reference: ray walking.
Bitboard ray_attacks(Square s, Bitboard occ, bool rook) {
    static const int rd[4][2] = {{0, 1}, {0, -1}, {1, 0}, {-1, 0}};
    static const int bd[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    Bitboard res = 0;
    for (int i = 0; i < 4; ++i) {
        const int df = rook ? rd[i][0] : bd[i][0], dr = rook ? rd[i][1] : bd[i][1];
        int f = file_of(s) + df, r = rank_of(s) + dr;
        while (f >= 0 && f < 8 && r >= 0 && r < 8) {
            const Bitboard b = 1ULL << (r * 8 + f);
            res |= b;
            if (occ & b) break;
            f += df;
            r += dr;
        }
    }
    return res;
}

struct Rng {
    std::uint64_t s = 88172645463325252ULL;
    std::uint64_t next() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return s;
    }
};

}  // namespace

TEST(bitboard_basics) {
    CHECK_EQ(popcount(0xFFULL), 8);
    CHECK_EQ(int(lsb(0x8000ULL)), 15);
    Bitboard b = 0b101;
    CHECK_EQ(int(pop_lsb(b)), 0);
    CHECK_EQ(b, Bitboard(0b100));
    CHECK(more_than_one(0b11));
    CHECK(!more_than_one(0b10));
    // No wrap-around between the A and H files.
    CHECK_EQ(shift<EAST>(square_bb(SQ_H4)), Bitboard(0));
    CHECK_EQ(shift<WEST>(square_bb(SQ_A4)), Bitboard(0));
    CHECK_EQ(shift<NORTH_EAST>(square_bb(SQ_H1)), Bitboard(0));
    CHECK_EQ(shift<NORTH>(square_bb(SQ_E2)), square_bb(SQ_E3));
}

TEST(leaper_attacks) {
    CHECK_EQ(popcount(knight_attacks(SQ_A1)), 2);
    CHECK_EQ(popcount(knight_attacks(SQ_D4)), 8);
    CHECK_EQ(popcount(king_attacks(SQ_A1)), 3);
    CHECK_EQ(popcount(king_attacks(SQ_E4)), 8);
    CHECK_EQ(pawn_attacks(WHITE, SQ_E2), square_bb(SQ_D3) | square_bb(SQ_F3));
    CHECK_EQ(pawn_attacks(WHITE, SQ_A2), square_bb(SQ_B3));
    CHECK_EQ(pawn_attacks(BLACK, SQ_H7), square_bb(SQ_G6));
    CHECK_EQ(pawn_attacks(WHITE, SQ_E8), Bitboard(0));
}

TEST(slider_attacks_match_reference) {
    Rng rng;
    for (int s = 0; s < 64; ++s) {
        for (int i = 0; i < 3000; ++i) {
            // Mix of sparse and dense occupancies.
            Bitboard occ = rng.next();
            if (i % 3 == 0) occ &= rng.next();
            if (i % 5 == 0) occ &= rng.next();
            CHECK_EQ(rook_attacks(Square(s), occ), ray_attacks(Square(s), occ, true));
            CHECK_EQ(bishop_attacks(Square(s), occ), ray_attacks(Square(s), occ, false));
        }
        CHECK_EQ(popcount(rook_attacks(Square(s), 0)), 14);
    }
}
