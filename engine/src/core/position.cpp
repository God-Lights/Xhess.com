#include "position.h"

#include <algorithm>
#include <sstream>

#include "init.h"
#include "zobrist.h"

namespace albatross {

namespace {

constexpr const char* PieceChars = " PNBRQK  pnbrqk";

// Castling rights that survive when a piece moves from or to a square.
struct CastleMasks {
    int mask[SQUARE_NB];
    constexpr CastleMasks() : mask() {
        for (int& m : mask) m = ANY_CASTLING;
        mask[SQ_A1] = ANY_CASTLING & ~WHITE_OOO;
        mask[SQ_H1] = ANY_CASTLING & ~WHITE_OO;
        mask[SQ_E1] = ANY_CASTLING & ~(WHITE_OO | WHITE_OOO);
        mask[SQ_A8] = ANY_CASTLING & ~BLACK_OOO;
        mask[SQ_H8] = ANY_CASTLING & ~BLACK_OO;
        mask[SQ_E8] = ANY_CASTLING & ~(BLACK_OO | BLACK_OOO);
    }
};
constexpr CastleMasks Castle;

}  // namespace

Position::Position() {
    init();
    set(START_FEN);
}

Position::Position(Scratch) { init(); }

void Position::clear() {
    std::fill(std::begin(board_), std::end(board_), NO_PIECE);
    std::fill(std::begin(byType_), std::end(byType_), Bitboard(0));
    std::fill(std::begin(byColor_), std::end(byColor_), Bitboard(0));
    stm_ = WHITE;
    gamePly_ = 0;
    st_.clear();
    st_.reserve(1024);
    st_.emplace_back();
}

void Position::put_piece(Piece pc, Square s) {
    board_[s] = pc;
    byType_[type_of(pc)] |= square_bb(s);
    byColor_[color_of(pc)] |= square_bb(s);
}

void Position::remove_piece(Square s) {
    const Piece pc = board_[s];
    byType_[type_of(pc)] &= ~square_bb(s);
    byColor_[color_of(pc)] &= ~square_bb(s);
    board_[s] = NO_PIECE;
}

void Position::move_piece(Square from, Square to) {
    const Piece pc = board_[from];
    const Bitboard ft = square_bb(from) | square_bb(to);
    byType_[type_of(pc)] ^= ft;
    byColor_[color_of(pc)] ^= ft;
    board_[from] = NO_PIECE;
    board_[to] = pc;
}

bool Position::ep_capturable(Color capturer, Square ep) const {
    return pawn_attacks(~capturer, ep) & pieces(capturer, PAWN);
}

Bitboard Position::attackers_to(Square s, Bitboard occupied) const {
    return (pawn_attacks(BLACK, s) & pieces(WHITE, PAWN)) | (pawn_attacks(WHITE, s) & pieces(BLACK, PAWN)) |
           (knight_attacks(s) & pieces(KNIGHT)) | (king_attacks(s) & pieces(KING)) |
           (rook_attacks(s, occupied) & pieces(ROOK, QUEEN)) | (bishop_attacks(s, occupied) & pieces(BISHOP, QUEEN));
}

// ---------------------------------------------------------------------------------------------
// FEN
// ---------------------------------------------------------------------------------------------

bool Position::set(const std::string& fen) {
    init();
    Position p{Scratch{}};  // build on a scratch copy so a bad FEN cannot corrupt *this
    p.clear();

    std::istringstream is(fen);
    std::string placement, side, castling, ep;
    if (!(is >> placement >> side >> castling >> ep)) return false;
    int halfmove = 0, fullmove = 1;
    if (is >> halfmove) {
        if (!(is >> fullmove)) fullmove = 1;
    }
    if (halfmove < 0 || fullmove < 1) return false;

    // 1. Piece placement
    int rank = 7, file = 0;
    for (char c : placement) {
        if (c == '/') {
            if (file != 8 || rank == 0) return false;
            --rank;
            file = 0;
        } else if (c >= '1' && c <= '8') {
            file += c - '0';
            if (file > 8) return false;
        } else {
            const char* pos = std::find(PieceChars, PieceChars + 15, c);
            if (c == ' ' || pos == PieceChars + 15 || file > 7) return false;
            const Piece pc = Piece(pos - PieceChars);
            const Square s = make_square(file, rank);
            if (type_of(pc) == PAWN && (rank == 0 || rank == 7)) return false;
            p.put_piece(pc, s);
            ++file;
        }
    }
    if (rank != 0 || file != 8) return false;
    for (Color c : {WHITE, BLACK})
        if (popcount(p.pieces(c, KING)) != 1) return false;

    // 2. Side to move
    if (side == "w") p.stm_ = WHITE;
    else if (side == "b") p.stm_ = BLACK;
    else return false;

    // 3. Castling rights; silently drop rights whose king/rook are not on their home squares.
    StateInfo& s = p.st_.back();
    if (castling != "-") {
        for (char c : castling) {
            switch (c) {
                case 'K': s.castling |= WHITE_OO; break;
                case 'Q': s.castling |= WHITE_OOO; break;
                case 'k': s.castling |= BLACK_OO; break;
                case 'q': s.castling |= BLACK_OOO; break;
                default: return false;
            }
        }
    }
    auto has = [&](Piece pc, Square sq) { return p.board_[sq] == pc; };
    if (!(has(W_KING, SQ_E1) && has(W_ROOK, SQ_H1))) s.castling &= ~WHITE_OO;
    if (!(has(W_KING, SQ_E1) && has(W_ROOK, SQ_A1))) s.castling &= ~WHITE_OOO;
    if (!(has(B_KING, SQ_E8) && has(B_ROOK, SQ_H8))) s.castling &= ~BLACK_OO;
    if (!(has(B_KING, SQ_E8) && has(B_ROOK, SQ_A8))) s.castling &= ~BLACK_OOO;

    // 4. En passant square: must be on the right rank, empty, with the double-pushed pawn behind it.
    if (ep != "-") {
        if (ep.size() != 2 || ep[0] < 'a' || ep[0] > 'h') return false;
        const int f = ep[0] - 'a';
        const int r = ep[1] - '1';
        const int wantRank = p.stm_ == WHITE ? 5 : 2;
        if (r != wantRank) return false;
        const Square eps = make_square(f, r);
        const Square pawnSq = eps - pawn_push(p.stm_);
        if (p.board_[eps] != NO_PIECE || p.board_[pawnSq] != make_piece(~p.stm_, PAWN)) return false;
        s.ep = eps;
    }

    s.rule50 = halfmove;
    p.gamePly_ = 2 * (fullmove - 1) + (p.stm_ == BLACK);

    // 5. Legality: the side that just moved must not be in check.
    if (p.attackers_to(p.king_square(~p.stm_)) & p.pieces(p.stm_)) return false;

    s.checkers = p.attackers_to(p.king_square(p.stm_)) & p.pieces(~p.stm_);
    s.key = p.compute_key();

    *this = std::move(p);
    return true;
}

std::string Position::fen() const {
    std::ostringstream os;
    for (int r = 7; r >= 0; --r) {
        int empty = 0;
        for (int f = 0; f < 8; ++f) {
            const Piece pc = board_[make_square(f, r)];
            if (pc == NO_PIECE) {
                ++empty;
                continue;
            }
            if (empty) os << empty;
            empty = 0;
            os << PieceChars[pc];
        }
        if (empty) os << empty;
        if (r) os << '/';
    }
    os << (stm_ == WHITE ? " w " : " b ");
    const int c = castling_rights();
    if (!c) os << '-';
    else {
        if (c & WHITE_OO) os << 'K';
        if (c & WHITE_OOO) os << 'Q';
        if (c & BLACK_OO) os << 'k';
        if (c & BLACK_OOO) os << 'q';
    }
    os << ' ';
    if (ep_square() == SQ_NONE) os << '-';
    else os << char('a' + file_of(ep_square())) << char('1' + rank_of(ep_square()));
    os << ' ' << rule50() << ' ' << fullmove();
    return os.str();
}

std::string Position::pretty() const {
    std::ostringstream os;
    for (int r = 7; r >= 0; --r) {
        os << " " << r + 1 << "  ";
        for (int f = 0; f < 8; ++f) {
            const Piece pc = board_[make_square(f, r)];
            os << (pc == NO_PIECE ? '.' : PieceChars[pc]) << ' ';
        }
        os << '\n';
    }
    os << "\n    a b c d e f g h\n\nFen: " << fen() << "\nKey: " << std::hex << key() << std::dec << '\n';
    return os.str();
}

// ---------------------------------------------------------------------------------------------
// Hashing and consistency
// ---------------------------------------------------------------------------------------------

Key Position::compute_key() const {
    Key k = 0;
    for (int i = 0; i < SQUARE_NB; ++i)
        if (board_[i] != NO_PIECE) k ^= Zobrist::psq[board_[i]][i];
    k ^= Zobrist::castling[castling_rights()];
    if (ep_square() != SQ_NONE && ep_capturable(stm_, ep_square())) k ^= Zobrist::enpassant[file_of(ep_square())];
    if (stm_ == BLACK) k ^= Zobrist::side;
    return k;
}

bool Position::is_ok() const {
    if (byColor_[WHITE] & byColor_[BLACK]) return false;
    Bitboard seen = 0;
    for (int t = PAWN; t <= KING; ++t) {
        if (byType_[t] & seen) return false;
        seen |= byType_[t];
    }
    if (seen != pieces()) return false;
    for (int i = 0; i < SQUARE_NB; ++i) {
        const Piece pc = board_[i];
        const Bitboard b = square_bb(Square(i));
        if (pc == NO_PIECE) {
            if (pieces() & b) return false;
        } else if (!(byType_[type_of(pc)] & b) || !(byColor_[color_of(pc)] & b)) {
            return false;
        }
    }
    for (Color c : {WHITE, BLACK})
        if (popcount(pieces(c, KING)) != 1) return false;
    if (st().key != compute_key()) return false;
    if (st().checkers != (attackers_to(king_square(stm_)) & pieces(~stm_))) return false;
    return true;
}

// ---------------------------------------------------------------------------------------------
// Legality
// ---------------------------------------------------------------------------------------------

bool Position::legal(Move m) const {
    const Color us = stm_, them = ~us;
    const Square from = m.from(), to = m.to();
    const Bitboard enemies = pieces(them);

    if (m.is_castling()) {
        if (in_check()) return false;
        // The king crosses one square and lands on another; neither may be attacked.
        const Square mid = m.flag() == KING_CASTLE ? to - 1 : to + 1;
        const Bitboard occ = pieces();
        return !(attackers_to(mid, occ) & enemies) && !(attackers_to(to, occ) & enemies);
    }

    // Pretend the move was played and test whether our king can be captured.
    Bitboard occ = (pieces() ^ square_bb(from)) | square_bb(to);
    Bitboard captured = 0;
    if (m.flag() == EP_CAPTURE) {
        captured = square_bb(to - pawn_push(us));
        occ ^= captured;
    } else if (m.is_capture()) {
        captured = square_bb(to);
    }
    const Square ksq = type_of(board_[from]) == KING ? to : king_square(us);
    return !(attackers_to(ksq, occ) & enemies & ~captured);
}

// ---------------------------------------------------------------------------------------------
// Make / unmake
// ---------------------------------------------------------------------------------------------

void Position::do_move(Move m) {
    const Color us = stm_, them = ~us;
    const Square from = m.from(), to = m.to();
    const MoveFlag f = m.flag();
    const Piece pc = board_[from];

    StateInfo ns = st_.back();  // copy: the previous state stays untouched for undo
    ns.move = m;
    ns.captured = NO_PIECE;
    Key k = ns.key;

    // The old en passant square stops mattering. (Checked before the board changes.)
    if (ns.ep != SQ_NONE) {
        if (ep_capturable(us, ns.ep)) k ^= Zobrist::enpassant[file_of(ns.ep)];
        ns.ep = SQ_NONE;
    }

    if (f == EP_CAPTURE) {
        const Square cs = to - pawn_push(us);
        ns.captured = make_piece(them, PAWN);
        remove_piece(cs);
        k ^= Zobrist::psq[ns.captured][cs];
    } else if (m.is_capture()) {
        ns.captured = board_[to];
        remove_piece(to);
        k ^= Zobrist::psq[ns.captured][to];
    }

    if (m.is_promotion()) {
        const Piece promoted = make_piece(us, m.promotion_type());
        remove_piece(from);
        put_piece(promoted, to);
        k ^= Zobrist::psq[pc][from] ^ Zobrist::psq[promoted][to];
    } else {
        move_piece(from, to);
        k ^= Zobrist::psq[pc][from] ^ Zobrist::psq[pc][to];
    }

    if (m.is_castling()) {
        const bool kingSide = f == KING_CASTLE;
        const Square rfrom = kingSide ? to + 1 : to - 2;
        const Square rto = kingSide ? to - 1 : to + 1;
        const Piece rook = make_piece(us, ROOK);
        move_piece(rfrom, rto);
        k ^= Zobrist::psq[rook][rfrom] ^ Zobrist::psq[rook][rto];
    }

    if (f == DOUBLE_PUSH) ns.ep = from + pawn_push(us);

    k ^= Zobrist::castling[ns.castling];
    ns.castling &= Castle.mask[from] & Castle.mask[to];
    k ^= Zobrist::castling[ns.castling];

    ns.rule50 = (type_of(pc) == PAWN || ns.captured != NO_PIECE) ? 0 : ns.rule50 + 1;

    stm_ = them;
    k ^= Zobrist::side;
    if (ns.ep != SQ_NONE && ep_capturable(them, ns.ep)) k ^= Zobrist::enpassant[file_of(ns.ep)];

    ns.checkers = attackers_to(king_square(them)) & pieces(us);
    ns.key = k;
    ++gamePly_;
    st_.push_back(ns);
}

void Position::undo_move() {
    const StateInfo s = st_.back();
    st_.pop_back();
    --gamePly_;
    stm_ = ~stm_;

    const Color us = stm_;
    const Move m = s.move;
    const Square from = m.from(), to = m.to();

    if (m.is_promotion()) {
        remove_piece(to);
        put_piece(make_piece(us, PAWN), from);
    } else {
        move_piece(to, from);
    }

    if (m.is_castling()) {
        const bool kingSide = m.flag() == KING_CASTLE;
        move_piece(kingSide ? to - 1 : to + 1, kingSide ? to + 1 : to - 2);
    }

    if (m.flag() == EP_CAPTURE) put_piece(s.captured, to - pawn_push(us));
    else if (m.is_capture()) put_piece(s.captured, to);
}

// ---------------------------------------------------------------------------------------------
// Draw detection
// ---------------------------------------------------------------------------------------------

int Position::repetition_count() const {
    int count = 1;
    const int n = int(st_.size());
    const int limit = std::min(rule50(), n - 1);
    // Same side to move => an even number of plies back; a repeat can't precede the last irreversible move.
    for (int back = 4; back <= limit; back += 2)
        if (st_[n - 1 - back].key == st().key) ++count;
    return count;
}

bool Position::insufficient_material() const {
    if (pieces(PAWN, ROOK) || pieces(QUEEN)) return false;
    const Bitboard minors = pieces(KNIGHT, BISHOP);
    if (popcount(minors) <= 1) return true;           // K vs K, K+minor vs K
    if (pieces(KNIGHT)) return false;                 // two+ minors including a knight: mate is possible
    // Only bishops: dead only if they all stand on the same color complex.
    constexpr Bitboard LightSquares = 0x55AA55AA55AA55AAULL;
    const Bitboard bishops = pieces(BISHOP);
    return !(bishops & LightSquares) || !(bishops & ~LightSquares);
}

}  // namespace albatross
