#pragma once
// 16-bit move: bits 0-5 from, 6-11 to, 12-15 flag.

#include <string>

#include "types.h"

namespace albatross {

enum MoveFlag : int {
    QUIET = 0,
    DOUBLE_PUSH = 1,
    KING_CASTLE = 2,
    QUEEN_CASTLE = 3,
    CAPTURE = 4,        // bit 2 set <=> capture
    EP_CAPTURE = 5,
    PROMO_N = 8,        // bit 3 set <=> promotion; low 2 bits = piece (N, B, R, Q)
    PROMO_B = 9,
    PROMO_R = 10,
    PROMO_Q = 11,
    PROMO_CAP_N = 12,
    PROMO_CAP_B = 13,
    PROMO_CAP_R = 14,
    PROMO_CAP_Q = 15
};

class Move {
public:
    constexpr Move() = default;
    constexpr Move(Square from, Square to, MoveFlag flag)
        : data_(std::uint16_t(from | (to << 6) | (flag << 12))) {}

    constexpr Square from() const { return Square(data_ & 63); }
    constexpr Square to() const { return Square((data_ >> 6) & 63); }
    constexpr MoveFlag flag() const { return MoveFlag(data_ >> 12); }

    constexpr bool is_capture() const { return flag() & 4; }
    constexpr bool is_promotion() const { return flag() & 8; }
    constexpr bool is_castling() const { return flag() == KING_CASTLE || flag() == QUEEN_CASTLE; }
    constexpr PieceType promotion_type() const { return PieceType((flag() & 3) + KNIGHT); }

    // 0 (a1a1 quiet) is never a legal move, so it doubles as the "no move" value.
    constexpr bool is_none() const { return data_ == 0; }
    constexpr std::uint16_t raw() const { return data_; }

    constexpr bool operator==(const Move&) const = default;

    static constexpr Move none() { return Move(); }

private:
    std::uint16_t data_ = 0;
};

// Long algebraic (UCI) notation: e2e4, e7e8q. Castling is the king's two-square move (e1g1).
std::string to_uci(Move m);

}  // namespace albatross
