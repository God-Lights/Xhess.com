#include "movegen.h"

namespace albatross {

namespace {

void add_promotions(MoveList& out, Square from, Square to, bool capture) {
    const MoveFlag base = capture ? PROMO_CAP_N : PROMO_N;
    for (int i = 3; i >= 0; --i) out.add(from, to, MoveFlag(base + i));  // queen first
}

}  // namespace

void generate_pseudo(const Position& pos, MoveList& out) {
    const Color us = pos.side_to_move(), them = ~us;
    const Bitboard occ = pos.pieces();
    const Bitboard ours = pos.pieces(us);
    const Bitboard theirs = pos.pieces(them);
    const Bitboard empty = ~occ;

    // ---- Pawns ----
    const Bitboard pawns = pos.pieces(us, PAWN);
    const int up = pawn_push(us);
    const Bitboard promoRank = us == WHITE ? Rank8BB : Rank1BB;
    const Bitboard thirdRank = us == WHITE ? Rank3BB : Rank6BB;

    const Bitboard push1 = (us == WHITE ? shift<NORTH>(pawns) : shift<SOUTH>(pawns)) & empty;
    const Bitboard push2 = (us == WHITE ? shift<NORTH>(push1 & thirdRank) : shift<SOUTH>(push1 & thirdRank)) & empty;

    for (Bitboard b = push1 & ~promoRank; b;) {
        const Square to = pop_lsb(b);
        out.add(to - up, to, QUIET);
    }
    for (Bitboard b = push2; b;) {
        const Square to = pop_lsb(b);
        out.add(to - 2 * up, to, DOUBLE_PUSH);
    }
    for (Bitboard b = push1 & promoRank; b;) {
        const Square to = pop_lsb(b);
        add_promotions(out, to - up, to, false);
    }
    for (Bitboard p = pawns; p;) {
        const Square from = pop_lsb(p);
        for (Bitboard b = pawn_attacks(us, from) & theirs; b;) {
            const Square to = pop_lsb(b);
            if (square_bb(to) & promoRank) add_promotions(out, from, to, true);
            else out.add(from, to, CAPTURE);
        }
    }
    if (pos.ep_square() != SQ_NONE) {
        for (Bitboard b = pawn_attacks(them, pos.ep_square()) & pawns; b;)
            out.add(pop_lsb(b), pos.ep_square(), EP_CAPTURE);
    }

    // ---- Knights, bishops, rooks, queens, king ----
    for (int t = KNIGHT; t <= KING; ++t) {
        const PieceType pt = PieceType(t);
        for (Bitboard p = pos.pieces(us, pt); p;) {
            const Square from = pop_lsb(p);
            for (Bitboard b = attacks_bb(pt, from, occ) & ~ours; b;) {
                const Square to = pop_lsb(b);
                out.add(from, to, (theirs & square_bb(to)) ? CAPTURE : QUIET);
            }
        }
    }

    // ---- Castling (rights imply king and rook are home; attacked squares are tested in legal()) ----
    const int rights = pos.castling_rights();
    if (us == WHITE) {
        if ((rights & WHITE_OO) && !(occ & (square_bb(SQ_F1) | square_bb(SQ_G1)))) out.add(SQ_E1, SQ_G1, KING_CASTLE);
        if ((rights & WHITE_OOO) && !(occ & (square_bb(SQ_B1) | square_bb(SQ_C1) | square_bb(SQ_D1))))
            out.add(SQ_E1, SQ_C1, QUEEN_CASTLE);
    } else {
        if ((rights & BLACK_OO) && !(occ & (square_bb(SQ_F8) | square_bb(SQ_G8)))) out.add(SQ_E8, SQ_G8, KING_CASTLE);
        if ((rights & BLACK_OOO) && !(occ & (square_bb(SQ_B8) | square_bb(SQ_C8) | square_bb(SQ_D8))))
            out.add(SQ_E8, SQ_C8, QUEEN_CASTLE);
    }
}

void generate_legal(const Position& pos, MoveList& out) {
    MoveList pseudo;
    generate_pseudo(pos, pseudo);
    for (Move m : pseudo)
        if (pos.legal(m)) out.add(m);
}

Move parse_uci_move(const Position& pos, const std::string& s) {
    MoveList list;
    generate_legal(pos, list);
    for (Move m : list)
        if (to_uci(m) == s) return m;
    return Move::none();
}

}  // namespace albatross
