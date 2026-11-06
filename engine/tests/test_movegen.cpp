#include <algorithm>
#include <string>

#include "core/movegen.h"
#include "test.h"

using namespace albatross;

namespace {

bool has_move(const Position& p, const std::string& uci) {
    MoveList l;
    generate_legal(p, l);
    return std::any_of(l.begin(), l.end(), [&](Move m) { return to_uci(m) == uci; });
}

std::size_t count_legal(const Position& p) {
    MoveList l;
    generate_legal(p, l);
    return l.size();
}

}  // namespace

TEST(move_encoding) {
    const Move m(SQ_E7, SQ_E8, PROMO_CAP_Q);
    CHECK_EQ(int(m.from()), int(SQ_E7));
    CHECK_EQ(int(m.to()), int(SQ_E8));
    CHECK(m.is_capture());
    CHECK(m.is_promotion());
    CHECK_EQ(int(m.promotion_type()), int(QUEEN));
    CHECK_EQ(to_uci(m), std::string("e7e8q"));
    CHECK_EQ(to_uci(Move(SQ_A7, SQ_A8, PROMO_N)), std::string("a7a8n"));
    CHECK(!Move(SQ_E2, SQ_E4, DOUBLE_PUSH).is_capture());
    CHECK(Move(SQ_E5, SQ_D6, EP_CAPTURE).is_capture());
    CHECK(Move::none().is_none());
    CHECK_EQ(sizeof(Move), std::size_t(2));
}

TEST(castling_moves_and_rights) {
    Position p;
    CHECK(p.set("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1"));
    CHECK(has_move(p, "e1g1"));
    CHECK(has_move(p, "e1c1"));
    p.do_move(parse_uci_move(p, "e1g1"));
    CHECK_EQ(int(p.piece_on(SQ_G1)), int(W_KING));
    CHECK_EQ(int(p.piece_on(SQ_F1)), int(W_ROOK));
    CHECK_EQ(int(p.piece_on(SQ_H1)), int(NO_PIECE));
    CHECK_EQ(p.castling_rights(), int(BLACK_OO | BLACK_OOO));
    p.do_move(parse_uci_move(p, "e8c8"));
    CHECK_EQ(int(p.piece_on(SQ_C8)), int(B_KING));
    CHECK_EQ(int(p.piece_on(SQ_D8)), int(B_ROOK));
    CHECK_EQ(p.castling_rights(), 0);
    p.undo_move();
    p.undo_move();
    CHECK_EQ(p.fen(), std::string("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1"));
}

TEST(castling_is_blocked_by_attacks_and_pieces) {
    Position p;
    // Rook on f8 attacks f1: kingside castling crosses an attacked square. Queenside still fine.
    CHECK(p.set("4kr2/8/8/8/8/8/8/R3K2R w KQ - 0 1"));
    CHECK(!has_move(p, "e1g1"));
    CHECK(has_move(p, "e1c1"));
    // In check: no castling at all.
    CHECK(p.set("4k3/8/8/8/8/8/4r3/R3K2R w KQ - 0 1"));
    CHECK(!has_move(p, "e1g1"));
    CHECK(!has_move(p, "e1c1"));
    // b1 may be attacked (only the king's path matters) but must be empty.
    CHECK(p.set("1r2k3/8/8/8/8/8/8/R3K3 w Q - 0 1"));
    CHECK(has_move(p, "e1c1"));
    CHECK(p.set("4k3/8/8/8/8/8/8/RN2K3 w Q - 0 1"));
    CHECK(!has_move(p, "e1c1"));
    // Rook captured on its home square removes the right.
    CHECK(p.set("r3k3/8/8/8/8/8/8/B3K3 b q - 0 1"));
    CHECK_EQ(p.castling_rights(), int(BLACK_OOO));
}

TEST(en_passant) {
    Position p;
    CHECK(p.set("rnbqkbnr/ppp1pppp/8/8/3pP3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 3"));
    CHECK(has_move(p, "d4e3"));
    const std::string before = p.fen();
    p.do_move(parse_uci_move(p, "d4e3"));
    CHECK_EQ(int(p.piece_on(SQ_E4)), int(NO_PIECE));  // captured pawn is removed from e4, not e3
    CHECK_EQ(int(p.piece_on(SQ_E3)), int(B_PAWN));
    CHECK(p.is_ok());
    p.undo_move();
    CHECK_EQ(p.fen(), before);
}

TEST(en_passant_that_exposes_the_king_is_illegal) {
    Position p;
    // Capturing d4xe3 removes both pawns from the 4th rank and opens the rook's line to the black king.
    CHECK(p.set("8/8/8/8/k2pP2R/8/8/4K3 b - e3 0 1"));
    CHECK(!has_move(p, "d4e3"));
    CHECK(has_move(p, "d4d3"));
}

TEST(pinned_pieces_cannot_leave_the_pin) {
    Position p;
    CHECK(p.set("4r1k1/8/8/8/8/8/4B3/4K3 w - - 0 1"));  // Be2 pinned on the e-file
    CHECK(!has_move(p, "e2d3"));
    CHECK(has_move(p, "e1d1"));
}

TEST(promotions_including_underpromotion) {
    Position p;
    CHECK(p.set("1n2k3/P7/8/8/8/8/8/4K3 w - - 0 1"));
    for (const char* m : {"a7a8q", "a7a8r", "a7a8b", "a7a8n", "a7b8q", "a7b8r", "a7b8b", "a7b8n"})
        CHECK(has_move(p, m));
    p.do_move(parse_uci_move(p, "a7b8n"));
    CHECK_EQ(int(p.piece_on(SQ_B8)), int(W_KNIGHT));
    p.undo_move();
    CHECK_EQ(int(p.piece_on(SQ_A7)), int(W_PAWN));
    CHECK_EQ(int(p.piece_on(SQ_B8)), int(B_KNIGHT));
}

TEST(checkmate_and_stalemate) {
    Position p;
    for (const char* m : {"f2f3", "e7e5", "g2g4", "d8h4"}) p.do_move(parse_uci_move(p, m));
    CHECK(p.in_check());
    CHECK_EQ(count_legal(p), std::size_t(0));  // fool's mate

    CHECK(p.set("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1"));
    CHECK(!p.in_check());
    CHECK_EQ(count_legal(p), std::size_t(0));  // stalemate
}

TEST(check_evasions) {
    Position p;
    // Nd3 checks Ke1: the knight can't be captured or blocked, so only king moves remain
    // (d1, d2, e2, f1; f2 is covered by the knight).
    CHECK(p.set("4k3/8/8/8/8/3n4/8/4K3 w - - 0 1"));
    CHECK(p.in_check());
    CHECK_EQ(count_legal(p), std::size_t(4));
    CHECK(!has_move(p, "e1f2"));

    // Re2 checks Ke1 and is undefended: Kxe2 is legal, the rank-2 squares are not.
    CHECK(p.set("4k3/8/8/8/8/8/4r3/R3K3 w - - 0 1"));
    CHECK(p.in_check());
    CHECK(has_move(p, "e1e2"));
    CHECK(!has_move(p, "e1d2"));
    CHECK(!has_move(p, "a1a2"));  // does not address the check
    CHECK_EQ(count_legal(p), std::size_t(3));
}
