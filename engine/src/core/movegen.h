#pragma once

#include <cstddef>

#include "move.h"
#include "position.h"

namespace albatross {

constexpr std::size_t MAX_MOVES = 256;

class MoveList {
public:
    void add(Square from, Square to, MoveFlag flag) { moves_[size_++] = Move(from, to, flag); }
    void add(Move m) { moves_[size_++] = m; }
    std::size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    const Move* begin() const { return moves_; }
    const Move* end() const { return moves_ + size_; }
    Move operator[](std::size_t i) const { return moves_[i]; }

private:
    Move moves_[MAX_MOVES];
    std::size_t size_ = 0;
};

// All pseudo-legal moves: may leave the own king in check (castling transit squares are checked by legal()).
void generate_pseudo(const Position& pos, MoveList& out);

// Strictly legal moves.
void generate_legal(const Position& pos, MoveList& out);

// Finds the legal move matching a UCI string, or Move::none().
Move parse_uci_move(const Position& pos, const std::string& s);

}  // namespace albatross
