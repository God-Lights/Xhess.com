#pragma once

#include <bit>
#include <string>

#include "types.h"

namespace albatross {

constexpr Bitboard FileABB = 0x0101010101010101ULL;
constexpr Bitboard FileHBB = FileABB << 7;
constexpr Bitboard Rank1BB = 0xFFULL;
constexpr Bitboard Rank2BB = Rank1BB << 8;
constexpr Bitboard Rank3BB = Rank1BB << 16;
constexpr Bitboard Rank6BB = Rank1BB << 40;
constexpr Bitboard Rank7BB = Rank1BB << 48;
constexpr Bitboard Rank8BB = Rank1BB << 56;

constexpr Bitboard square_bb(Square s) { return 1ULL << s; }

inline int popcount(Bitboard b) { return std::popcount(b); }
inline Square lsb(Bitboard b) { return Square(std::countr_zero(b)); }
inline Square pop_lsb(Bitboard& b) {
    Square s = lsb(b);
    b &= b - 1;
    return s;
}
constexpr bool more_than_one(Bitboard b) { return b & (b - 1); }

template <Direction D>
constexpr Bitboard shift(Bitboard b) {
    return D == NORTH        ? b << 8
         : D == SOUTH        ? b >> 8
         : D == EAST         ? (b & ~FileHBB) << 1
         : D == WEST         ? (b & ~FileABB) >> 1
         : D == NORTH_EAST   ? (b & ~FileHBB) << 9
         : D == NORTH_WEST   ? (b & ~FileABB) << 7
         : D == SOUTH_EAST   ? (b & ~FileHBB) >> 7
         : D == SOUTH_WEST   ? (b & ~FileABB) >> 9
                             : 0;
}

// For debugging: 8x8 ASCII picture of a bitboard.
std::string pretty(Bitboard b);

}  // namespace albatross
