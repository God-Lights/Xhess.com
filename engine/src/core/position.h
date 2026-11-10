#pragma once

#include <string>
#include <vector>

#include "attacks.h"
#include "bitboard.h"
#include "move.h"
#include "types.h"

namespace albatross {

// Everything do_move() cannot recompute cheaply from the board. One entry per ply; undo_move() pops it.
struct StateInfo {
    Key key = 0;
    Bitboard checkers = 0;     // enemy pieces giving check to the side to move
    Move move;                 // move that led to this state (none for the root)
    Piece captured = NO_PIECE;
    Square ep = SQ_NONE;       // FEN-style: set after every double push, even if nobody can capture
    int castling = NO_CASTLING;
    int rule50 = 0;
};

class Position {
public:
    static constexpr const char* START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

    Position();

    // Returns false (and leaves *this untouched) if the FEN is malformed or the position is illegal.
    bool set(const std::string& fen);
    std::string fen() const;
    std::string pretty() const;  // board diagram for the UCI "d" command

    Piece piece_on(Square s) const { return board_[s]; }
    Bitboard pieces() const { return byColor_[WHITE] | byColor_[BLACK]; }
    Bitboard pieces(PieceType pt) const { return byType_[pt]; }
    Bitboard pieces(PieceType a, PieceType b) const { return byType_[a] | byType_[b]; }
    Bitboard pieces(Color c) const { return byColor_[c]; }
    Bitboard pieces(Color c, PieceType pt) const { return byColor_[c] & byType_[pt]; }
    Square king_square(Color c) const { return lsb(pieces(c, KING)); }

    Color side_to_move() const { return stm_; }
    Square ep_square() const { return st().ep; }
    int castling_rights() const { return st().castling; }
    int rule50() const { return st().rule50; }
    int game_ply() const { return gamePly_; }
    int fullmove() const { return gamePly_ / 2 + 1; }
    Key key() const { return st().key; }
    Bitboard checkers() const { return st().checkers; }
    bool in_check() const { return st().checkers != 0; }
    Move last_move() const { return st().move; }

    // Pieces of both colors attacking s, given an explicit occupancy (lets legality tests "pretend").
    Bitboard attackers_to(Square s, Bitboard occupied) const;
    Bitboard attackers_to(Square s) const { return attackers_to(s, pieces()); }

    // m must be pseudo-legal for this position (as produced by generate_pseudo).
    bool legal(Move m) const;

    void do_move(Move m);
    void undo_move();

    // Passes the turn without moving (null-move pruning). Only valid when not in check.
    void do_null_move();
    void undo_null_move();

    // Non-pawn, non-king material of the side: false when it only has king and pawns (zugzwang-prone).
    bool has_non_pawn_material(Color c) const { return pieces(c) & ~pieces(PAWN, KING); }

    // Draw rules.
    int repetition_count() const;          // occurrences of the current position in the game so far (>= 1)
    bool insufficient_material() const;    // dead position by material (FIDE 5.2.2 subset)
    bool is_fifty_move_draw() const { return st().rule50 >= 100; }      // claimable
    bool is_seventyfive_move_draw() const { return st().rule50 >= 150; }  // automatic
    bool is_fivefold_repetition() const { return repetition_count() >= 5; }

    // Releases the spare capacity of the state stack. A position that will only be evaluated (tuning data)
    // then takes a few hundred bytes instead of several kilobytes.
    void compact() { st_.shrink_to_fit(); }

    // Debug helpers.
    Key compute_key() const;   // recompute the Zobrist key from scratch
    bool is_ok() const;        // board/bitboards/key/checkers consistency

private:
    struct Scratch {};
    explicit Position(Scratch);  // empty board, used by set() to build on a scratch copy

    const StateInfo& st() const { return st_.back(); }
    void put_piece(Piece pc, Square s);
    void remove_piece(Square s);
    void move_piece(Square from, Square to);
    bool ep_capturable(Color capturer, Square ep) const;
    void clear();

    Piece board_[SQUARE_NB];
    Bitboard byType_[PIECE_TYPE_NB];
    Bitboard byColor_[COLOR_NB];
    Color stm_ = WHITE;
    int gamePly_ = 0;
    std::vector<StateInfo> st_;
};

}  // namespace albatross
