// Hand-crafted evaluation, version 0.4 "Position Sense".
//
// Every term produces a middlegame and an endgame value; the two are blended by the game phase
// (amount of non-pawn material), so the same code handles an opening, a middlegame and a bare endgame.
// The terms are kept separate (EvalTrace) so the evaluation can be explained, not just computed.
//
// Tables are written from White's point of view with rank 1 first and file A first, which matches the
// LERF square numbering (A1 = 0). Black uses the vertically mirrored square (sq ^ 56).
// All weights are hand-written starting points and will be tuned by self-play (0.5).

#include <algorithm>
#include <cstdio>
#include <map>
#include <cstdlib>
#include <sstream>

#include "evaluation.h"
#include "params.h"
#include "score.h"

namespace albatross {

namespace {

// Game phase weights: 24 at the starting position, 0 with only kings and pawns. (Not tuned.)
constexpr int PhaseWeight[PIECE_TYPE_NB] = {0, 0, 1, 1, 2, 4, 0};
constexpr int MaxPhase = 24;

// All weights come from params.def through EvalP (a constexpr object in the normal build).
const int* pstMg(PieceType pt) {
    switch (pt) {
        case PAWN: return EvalP.pstPawnMg;
        case KNIGHT: return EvalP.pstKnight;
        case BISHOP: return EvalP.pstBishop;
        case ROOK: return EvalP.pstRook;
        case QUEEN: return EvalP.pstQueen;
        default: return EvalP.pstKingMg;
    }
}
const int* pstEg(PieceType pt) {
    switch (pt) {
        case PAWN: return EvalP.pstPawnEg;
        case KNIGHT: return EvalP.pstKnight;
        case BISHOP: return EvalP.pstBishop;
        case ROOK: return EvalP.pstRook;
        case QUEEN: return EvalP.pstQueen;
        default: return EvalP.pstKingEg;
    }
}

// ---- bitboard helpers ----
struct Masks {
    Bitboard file[8];
    Bitboard adjacent[8];                 // neighbouring files
    Bitboard front[COLOR_NB][SQUARE_NB];  // squares ahead on the same and adjacent files (passed pawn test)
    Bitboard ahead[COLOR_NB][SQUARE_NB];  // squares ahead on the same file

    Masks() {
        for (int f = 0; f < 8; ++f) {
            file[f] = FileABB << f;
            adjacent[f] = (f > 0 ? FileABB << (f - 1) : 0) | (f < 7 ? FileABB << (f + 1) : 0);
        }
        for (int s = 0; s < 64; ++s) {
            const int f = file_of(Square(s)), r = rank_of(Square(s));
            Bitboard up = 0, down = 0;
            for (int rr = r + 1; rr < 8; ++rr) up |= Rank1BB << (8 * rr);
            for (int rr = r - 1; rr >= 0; --rr) down |= Rank1BB << (8 * rr);
            ahead[WHITE][s] = up & file[f];
            ahead[BLACK][s] = down & file[f];
            front[WHITE][s] = up & (file[f] | adjacent[f]);
            front[BLACK][s] = down & (file[f] | adjacent[f]);
        }
    }
};
const Masks M;

int relative_rank(Color c, Square s) { return c == WHITE ? rank_of(s) : 7 - rank_of(s); }
int distance(Square a, Square b) { return std::max(std::abs(file_of(a) - file_of(b)), std::abs(rank_of(a) - rank_of(b))); }
int center_distance(Square s) {
    const int f = file_of(s), r = rank_of(s);
    return std::max(3 - f, f - 4) + std::max(3 - r, r - 4);
}

Bitboard pawn_attacks_bb(Color c, Bitboard pawns) {
    return c == WHITE ? shift<NORTH_EAST>(pawns) | shift<NORTH_WEST>(pawns)
                      : shift<SOUTH_EAST>(pawns) | shift<SOUTH_WEST>(pawns);
}

// Collect = true gathers the strategic themes (evaluate_trace). Collect = false is the version search uses:
// all of the bookkeeping is compiled away, so collecting marks cannot slow the search down.
template <bool Collect>
class Evaluator {
public:
    explicit Evaluator(const Position& pos, std::vector<EvalMark>* marks = nullptr) : pos_(pos), marks_(marks) {}

    EvalTrace run() {
        EvalTrace t;
        for (Color c : {WHITE, BLACK}) {
            material_and_pst(c);
            pawns(c);
            pieces(c);
            king_safety(c);
        }
        endgame(WHITE);
        endgame(BLACK);

        int phase = 0;
        for (Color c : {WHITE, BLACK})
            for (int pt = KNIGHT; pt <= QUEEN; ++pt) phase += PhaseWeight[pt] * popcount(pos_.pieces(c, PieceType(pt)));
        t.phase = std::min(phase, MaxPhase);
        t.egScale = endgame_scale();

        int mg = 0, eg = 0;
        for (int i = 0; i < TERM_NB; ++i)
            for (Color c : {WHITE, BLACK}) {
                t.term[i][c] = {term_[i][c].mg, term_[i][c].eg};
                const int sign = c == WHITE ? 1 : -1;
                mg += sign * term_[i][c].mg;
                eg += sign * term_[i][c].eg;
            }
        eg = eg * t.egScale / 128;
        t.white = (mg * t.phase + eg * (MaxPhase - t.phase)) / MaxPhase;
        t.stm = pos_.side_to_move() == WHITE ? t.white : -t.white;
        return t;
    }

private:
    void add(EvalTerm term, Color c, Score s) { term_[term][c] += s; }
    void mark(Theme theme, Color c, Square sq, Score s) {
        if constexpr (Collect) marks_->push_back({theme, c, sq, s.mg, s.eg});
        else (void)theme, (void)c, (void)sq, (void)s;
    }

    void material_and_pst(Color c) {
        const int flip = c == WHITE ? 0 : 56;
        for (int t = PAWN; t <= KING; ++t) {
            const PieceType pt = PieceType(t);
            for (Bitboard b = pos_.pieces(c, pt); b;) {
                const Square s = pop_lsb(b);
                add(TERM_MATERIAL, c, S(EvalP.materialMg[pt], EvalP.materialEg[pt]));
                add(TERM_PST, c, S(pstMg(pt)[s ^ flip], pstEg(pt)[s ^ flip]));
            }
        }
        if (popcount(pos_.pieces(c, BISHOP)) >= 2) {
            add(TERM_IMBALANCE, c, EvalP.bishopPair);
            bool first = true;  // the first bishop carries the value, the second only marks its square
            for (Bitboard b = pos_.pieces(c, BISHOP); b; first = false) mark(THEME_BISHOP_PAIR, c, pop_lsb(b), first ? EvalP.bishopPair : S(0, 0));
        }
    }

    void pawns(Color c) {
        const Color them = ~c;
        const Bitboard ours = pos_.pieces(c, PAWN), theirs = pos_.pieces(them, PAWN);
        const Square ourKing = pos_.king_square(c), theirKing = pos_.king_square(them);

        for (Bitboard b = ours; b;) {
            const Square s = pop_lsb(b);
            const int f = file_of(s), r = relative_rank(c, s);
            const Bitboard bb = square_bb(s);

            if (ours & M.ahead[c][s]) {   // another own pawn stands in front of it
                add(TERM_PAWNS, c, EvalP.doubledPawn);
                mark(THEME_DOUBLED, c, s, EvalP.doubledPawn);
            }
            if (!(ours & M.adjacent[f])) {
                add(TERM_PAWNS, c, EvalP.isolatedPawn);
                mark(THEME_ISOLATED, c, s, EvalP.isolatedPawn);
            }
            if (pawn_attacks(them, s) & ours) add(TERM_PAWNS, c, S(EvalP.supportedPawnMg[r], EvalP.supportedPawnMg[r] / 2));
            if ((shift<EAST>(bb) | shift<WEST>(bb)) & ours) add(TERM_PAWNS, c, S(EvalP.phalanxPawnMg[r], EvalP.phalanxPawnMg[r] / 2));

            if (!(theirs & M.front[c][s]) && !(ours & M.ahead[c][s])) {
                // Passed pawn.
                const Square stop = s + pawn_push(c);
                Score bonus = S(EvalP.passedMg[r], EvalP.passedEg[r]);
                if (pos_.pieces() & square_bb(stop)) bonus = S(bonus.mg / 2, bonus.eg / 2);  // blocked
                // In the endgame the kings decide: reward our king being close, the enemy king being far.
                bonus.eg += (distance(theirKing, stop) * 5 - distance(ourKing, stop) * 2) * std::max(r - 1, 0) / 2;
                add(TERM_PASSED, c, bonus);
                mark(THEME_PASSED, c, s, bonus);
            }
        }
    }

    void pieces(Color c) {
        const Color them = ~c;
        const Bitboard occ = pos_.pieces();
        const Bitboard ours = pos_.pieces(c, PAWN), theirs = pos_.pieces(them, PAWN);
        const Bitboard area = ~pos_.pieces(c) & ~pawn_attacks_bb(them, theirs);  // squares worth counting

        for (int t = KNIGHT; t <= QUEEN; ++t) {
            const PieceType pt = PieceType(t);
            for (Bitboard b = pos_.pieces(c, pt); b;) {
                const Square s = pop_lsb(b);
                const int count = popcount(attacks_bb(pt, s, occ) & area);
                const Score mobility = S(EvalP.mobilityMg[pt], EvalP.mobilityEg[pt]) * (count - EvalP.mobilityBase[pt]);
                add(TERM_MOBILITY, c, mobility);

                const int f = file_of(s), r = relative_rank(c, s);
                // A piece that has left its back rank but still has almost nowhere to go.
                if (r >= 1 && ((pt == KNIGHT || pt == BISHOP) ? count <= 1 : (pt == ROOK && count <= 2))) mark(THEME_INACTIVE, c, s, mobility);
                if (pt == KNIGHT && r >= 3 && r <= 5 && (pawn_attacks(them, s) & ours) &&
                    !(theirs & M.adjacent[f] & M.front[c][s]))
                {
                    add(TERM_KNIGHTS, c, EvalP.knightOutpost);  // supported, and no enemy pawn can ever attack it
                    mark(THEME_OUTPOST, c, s, EvalP.knightOutpost);
                }
                if (pt == ROOK) {
                    if (!(ours & M.file[f])) {
                        const bool semi = theirs & M.file[f];
                        add(TERM_ROOKS, c, semi ? EvalP.rookSemiOpenFile : EvalP.rookOpenFile);
                        mark(semi ? THEME_ROOK_SEMI : THEME_ROOK_OPEN, c, s, semi ? EvalP.rookSemiOpenFile : EvalP.rookOpenFile);
                    }
                    if (r == 6) {
                        add(TERM_ROOKS, c, EvalP.rookOnSeventh);
                        mark(THEME_ROOK_SEVENTH, c, s, EvalP.rookOnSeventh);
                    }
                }
            }
        }
    }

    void king_safety(Color c) {
        const Color them = ~c;
        const Square ksq = pos_.king_square(c);
        const Bitboard ours = pos_.pieces(c, PAWN), theirs = pos_.pieces(them, PAWN);
        const int kf = file_of(ksq), kr = rank_of(ksq);
        const int dir = c == WHITE ? 1 : -1;

        // Pawn shield and open files around the king.
        int shield = 0;
        for (int f = std::max(0, kf - 1); f <= std::min(7, kf + 1); ++f) {
            const int r1 = kr + dir, r2 = kr + 2 * dir;
            const bool near = r1 >= 0 && r1 < 8 && (ours & square_bb(make_square(f, r1)));
            const bool far = r2 >= 0 && r2 < 8 && (ours & square_bb(make_square(f, r2)));
            const int fileOpen = !(ours & M.file[f]) ? ((theirs & M.file[f]) ? EvalP.kingFileSemiOpen : EvalP.kingFileOpen) : 0;
            if (near) shield += EvalP.shieldNear;
            else if (far) shield += EvalP.shieldFar;
            else {
                shield += EvalP.shieldMissing;
                // A hole in front of the king (plus an open file if no pawn of ours is left on it).
                mark(THEME_KING_SHIELD, c, (r1 >= 0 && r1 < 8) ? make_square(f, r1) : ksq, S(EvalP.shieldMissing + fileOpen, 0));
            }
            shield += fileOpen;
        }

        // Enemy pieces attacking the squares around the king (and one rank further forward).
        Bitboard zone = king_attacks(ksq) | square_bb(ksq);
        zone |= c == WHITE ? shift<NORTH>(zone) : shift<SOUTH>(zone);
        const Bitboard occ = pos_.pieces();
        int units = 0, attackers = 0;
        bool queen = false;
        Square attackerSquares[16];
        for (int t = KNIGHT; t <= QUEEN; ++t) {
            for (Bitboard b = pos_.pieces(them, PieceType(t)); b;) {
                const Square s = pop_lsb(b);
                if (attacks_bb(PieceType(t), s, occ) & zone) {
                    if constexpr (Collect) {
                        if (attackers < 16) attackerSquares[attackers] = s;
                    }
                    ++attackers;
                    units += EvalP.kingAttackUnit[t];
                    queen |= t == QUEEN;
                }
            }
        }
        int danger = 0;
        if (attackers >= 2) danger = std::min(units * units, 400) / (queen ? 1 : 2);
        if constexpr (Collect) {
            if (danger > 0) {
                // The first attacker carries the value, the others only mark their squares.
                for (int i = 0; i < std::min(attackers, 16); ++i) mark(THEME_KING_DANGER, c, attackerSquares[i], i == 0 ? S(-danger, 0) : S(0, 0));
            }
        }

        add(TERM_KING, c, S(shield - danger, 0));
    }

    // Endgame knowledge for side `c`: mop-up of a lone king, and unstoppable passed pawns.
    void endgame(Color c) {
        const Color them = ~c;
        const Square ourKing = pos_.king_square(c), theirKing = pos_.king_square(them);

        // The opponent has nothing but a king: drive it to the edge and bring our king close.
        const bool loneKing = !(pos_.pieces(them) & ~pos_.pieces(KING));
        const int heavy = popcount(pos_.pieces(c, QUEEN)) * 9 + popcount(pos_.pieces(c, ROOK)) * 5 +
                          popcount(pos_.pieces(c, BISHOP)) * 3 + popcount(pos_.pieces(c, KNIGHT)) * 3;
        if (loneKing && heavy >= 5) {
            const Score mopUp = S(0, EvalP.mopUpEdge * center_distance(theirKing) + EvalP.mopUpKing * (7 - distance(ourKing, theirKing)));
            add(TERM_ENDGAME, c, mopUp);
            mark(THEME_MOPUP, c, theirKing, mopUp);
        }

        // Rule of the square: if the enemy has no pieces, a passed pawn the enemy king cannot catch wins.
        if (pos_.has_non_pawn_material(them)) return;
        const Bitboard theirs = pos_.pieces(them, PAWN), ours = pos_.pieces(c, PAWN);
        for (Bitboard b = ours; b;) {
            const Square s = pop_lsb(b);
            if ((theirs & M.front[c][s]) || (ours & M.ahead[c][s]) || (pos_.pieces() & M.ahead[c][s])) continue;
            const int r = relative_rank(c, s);
            const Square promo = make_square(file_of(s), c == WHITE ? 7 : 0);
            int moves = 7 - r;
            if (r == 1) --moves;  // a pawn on its starting rank can advance two squares at once
            const int king = distance(theirKing, promo);
            const bool attackerToMove = pos_.side_to_move() == c;
            // The enemy king needs `king` moves; it moves second if the pawn's side is to move.
            if (attackerToMove ? king >= moves : king > moves) {
                add(TERM_ENDGAME, c, S(0, EvalP.unstoppablePasser));
                mark(THEME_UNSTOPPABLE, c, s, S(0, EvalP.unstoppablePasser));
            }
        }
    }

    // Opposite-colored bishops with no other pieces are very drawish: shrink the endgame value.
    int endgame_scale() const {
        const bool onlyBishops = popcount(pos_.pieces(WHITE, BISHOP)) == 1 && popcount(pos_.pieces(BLACK, BISHOP)) == 1 &&
                                 !pos_.pieces(KNIGHT, ROOK) && !pos_.pieces(QUEEN);
        if (!onlyBishops) return 128;
        constexpr Bitboard LightSquares = 0x55AA55AA55AA55AAULL;
        const bool whiteLight = pos_.pieces(WHITE, BISHOP) & LightSquares;
        const bool blackLight = pos_.pieces(BLACK, BISHOP) & LightSquares;
        return whiteLight != blackLight ? 64 : 128;
    }

    const Position& pos_;
    std::vector<EvalMark>* marks_;
    Score term_[TERM_NB][COLOR_NB] = {};
};

}  // namespace

EvalTrace evaluate_trace(const Position& pos) {
    std::vector<EvalMark> marks;
    EvalTrace t = Evaluator<true>(pos, &marks).run();
    t.marks = std::move(marks);
    return t;
}

int evaluate(const Position& pos) { return Evaluator<false>(pos).run().stm; }

const char* term_name(EvalTerm t) {
    static const char* names[TERM_NB] = {"Material", "Piece-square", "Bishop pair", "Pawn structure", "Passed pawns",
                                         "Mobility", "Knight outposts", "Rooks", "King safety", "Endgame"};
    return names[t];
}

const char* theme_id(Theme t) {
    static const char* ids[THEME_NB] = {"passed",      "unstoppable", "doubled",     "isolated",    "outpost",
                                        "rook_open",   "rook_semi",   "rook_seventh", "bishop_pair", "king_shield",
                                        "king_danger", "inactive",    "mopup"};
    return ids[t];
}

std::vector<ThemeSummary> summarize_themes(const EvalTrace& t) {
    std::map<std::pair<int, int>, ThemeSummary> by;   // (theme, color) in display order
    for (const EvalMark& m : t.marks) {
        ThemeSummary& s = by[{m.theme, m.color}];
        s.theme = m.theme;
        s.color = m.color;
        s.mg += m.mg;
        s.eg += m.eg;
        if (m.square != SQ_NONE && std::find(s.squares.begin(), s.squares.end(), m.square) == s.squares.end())
            s.squares.push_back(m.square);
    }
    std::vector<ThemeSummary> out;
    for (auto& [key, s] : by) {
        (void)key;
        s.cp = (s.mg * t.phase + s.eg * t.egScale / 128 * (MaxPhase - t.phase)) / MaxPhase;   // same blend as the total
        out.push_back(std::move(s));
    }
    return out;
}

std::string format_themes(const EvalTrace& t) {
    std::ostringstream os;
    for (const ThemeSummary& s : summarize_themes(t)) {
        os << "theme " << theme_id(s.theme) << (s.color == WHITE ? " white " : " black ");
        if (s.squares.empty()) os << '-';
        for (std::size_t i = 0; i < s.squares.size(); ++i)
            os << (i ? "," : "") << char('a' + file_of(s.squares[i])) << char('1' + rank_of(s.squares[i]));
        os << " cp " << s.cp << " mg " << s.mg << " eg " << s.eg << '\n';
    }
    os << "themes done phase " << t.phase << " scale " << t.egScale << " white " << t.white;
    return os.str();
}

std::string format_trace(const EvalTrace& t) {
    std::ostringstream os;
    char line[160];
    os << "     Term          |    White    |    Black    |    Total\n";
    os << "                   |  MG    EG   |  MG    EG   |  MG    EG\n";
    os << "-------------------+-------------+-------------+-------------\n";
    for (int i = 0; i < TERM_NB; ++i) {
        const TermScore &w = t.term[i][WHITE], &b = t.term[i][BLACK];
        std::snprintf(line, sizeof line, "%-18s | %5d %5d | %5d %5d | %5d %5d\n", term_name(EvalTerm(i)), w.mg, w.eg,
                      b.mg, b.eg, w.mg - b.mg, w.eg - b.eg);
        os << line;
    }
    os << "-------------------+-------------+-------------+-------------\n";
    std::snprintf(line, sizeof line, "Phase %d/24, endgame scale %d/128\nFinal evaluation: %+.2f (White's point of view), %+.2f (side to move)\n",
                  t.phase, t.egScale, t.white / 100.0, t.stm / 100.0);
    os << line;
    return os.str();
}

}  // namespace albatross
