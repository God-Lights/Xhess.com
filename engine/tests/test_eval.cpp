#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <vector>

#include "core/movegen.h"
#include "eval/evaluation.h"
#include "test.h"

using namespace albatross;

namespace {

// Color-flipped position: ranks reversed, piece colors swapped, side to move swapped.
std::string mirror_fen(const std::string& fen) {
    std::istringstream is(fen);
    std::string placement, side, castling, ep, rest;
    is >> placement >> side >> castling >> ep;
    std::getline(is, rest);

    std::vector<std::string> ranks;
    std::stringstream ps(placement);
    for (std::string r; std::getline(ps, r, '/');) ranks.push_back(r);
    std::reverse(ranks.begin(), ranks.end());
    std::string out;
    for (std::size_t i = 0; i < ranks.size(); ++i) {
        for (char c : ranks[i]) out += std::isalpha(static_cast<unsigned char>(c))
                                           ? (std::isupper(static_cast<unsigned char>(c)) ? char(std::tolower(c)) : char(std::toupper(c)))
                                           : c;
        if (i + 1 < ranks.size()) out += '/';
    }
    std::string c2;
    for (char c : castling)
        c2 += c == '-' ? c : (std::isupper(static_cast<unsigned char>(c)) ? char(std::tolower(c)) : char(std::toupper(c)));
    // Castling letters are conventionally ordered KQkq.
    std::sort(c2.begin(), c2.end(), [](char a, char b) {
        auto rank = [](char x) { return std::string("KQkq-").find(x); };
        return rank(a) < rank(b);
    });
    std::string ep2 = ep;
    if (ep != "-") ep2[1] = ep[1] == '3' ? '6' : '3';
    return out + (side == "w" ? " b " : " w ") + c2 + " " + ep2 + rest;
}

}  // namespace

TEST(eval_is_color_symmetric) {
    const char* fens[] = {
        Position::START_FEN,
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
        "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
        "4k3/8/8/8/8/8/4P3/4K3 b - - 0 1",
    };
    for (const char* f : fens) {
        Position a, b;
        CHECK(a.set(f));
        CHECK(b.set(mirror_fen(f)));
        CHECK_EQ(evaluate(a), evaluate(b));
    }
}

TEST(eval_start_position_is_balanced) {
    Position p;
    CHECK_EQ(evaluate(p), 0);
}

TEST(eval_prefers_more_material) {
    Position p;
    // White to move, up a queen.
    CHECK(p.set("rnb1kbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"));
    CHECK(evaluate(p) > 800);
    // Same position with Black to move: the score flips sign (side-to-move perspective).
    CHECK(p.set("rnb1kbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1"));
    CHECK(evaluate(p) < -800);
}

TEST(eval_king_prefers_center_in_endgame_and_corner_in_middlegame) {
    Position center, corner;
    // Kings and pawns only: the centralized king is better.
    CHECK(center.set("4k3/8/8/8/3K4/8/P7/8 w - - 0 1"));
    CHECK(corner.set("4k3/8/8/8/8/8/P7/7K w - - 0 1"));
    CHECK(evaluate(center) > evaluate(corner));
    // Full board: the castled king is better than one in the open.
    Position castled, open;
    CHECK(castled.set("r1bq1rk1/pppp1ppp/2n2n2/2b1p3/2B1P3/2N2N2/PPPP1PPP/R1BQ1RK1 w - - 0 1"));
    CHECK(open.set("r1bq1rk1/pppp1ppp/2n2n2/2b1p3/2B1P3/2N2N2/PPPPKPPP/R1BQ3R w - - 0 1"));
    CHECK(evaluate(castled) > evaluate(open));
}

// ---- 0.4 Position Sense: every term, one at a time ----

namespace {
EvalTrace trace(const char* fen) {
    Position p;
    const bool ok = p.set(fen);
    CHECK(ok);
    return evaluate_trace(p);
}
const TermScore& white(const EvalTrace& t, EvalTerm term) { return t.term[term][WHITE]; }
const TermScore& black(const EvalTrace& t, EvalTerm term) { return t.term[term][BLACK]; }
}  // namespace

TEST(eval_trace_matches_evaluate_and_terms_add_up) {
    const char* fens[] = {
        Position::START_FEN,
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "4k3/8/8/8/8/8/8/R3K3 b - - 0 1",
    };
    for (const char* f : fens) {
        Position p;
        CHECK(p.set(f));
        const EvalTrace t = evaluate_trace(p);
        CHECK_EQ(t.stm, evaluate(p));
        int mg = 0, eg = 0;
        for (int i = 0; i < TERM_NB; ++i) {
            mg += t.term[i][WHITE].mg - t.term[i][BLACK].mg;
            eg += t.term[i][WHITE].eg - t.term[i][BLACK].eg;
        }
        eg = eg * t.egScale / 128;
        CHECK_EQ(t.white, (mg * t.phase + eg * (24 - t.phase)) / 24);
        CHECK_EQ(t.stm, p.side_to_move() == WHITE ? t.white : -t.white);
    }
}

TEST(eval_phase_follows_material) {
    CHECK_EQ(trace(Position::START_FEN).phase, 24);
    CHECK_EQ(trace("4k3/pppppppp/8/8/8/8/PPPPPPPP/4K3 w - - 0 1").phase, 0);
    CHECK_EQ(trace("r3k2r/8/8/8/8/8/8/R3K2R w - - 0 1").phase, 8);
    // Promotions can push the sum above the starting value; it is clamped.
    CHECK_EQ(trace("4k3/8/8/8/8/8/8/QQQQKQQQ w - - 0 1").phase, 24);
}

TEST(eval_doubled_and_isolated_pawns_are_penalised) {
    const TermScore healthy = white(trace("4k3/8/8/8/8/1P6/2P5/4K3 w - - 0 1"), TERM_PAWNS);
    const TermScore doubled = white(trace("4k3/8/8/8/8/2P5/2P5/4K3 w - - 0 1"), TERM_PAWNS);
    CHECK(doubled.mg < healthy.mg);
    CHECK(doubled.eg < healthy.eg);
    const TermScore connected = white(trace("4k3/8/8/8/8/8/PP6/4K3 w - - 0 1"), TERM_PAWNS);
    const TermScore isolated = white(trace("4k3/8/8/8/8/8/P1P5/4K3 w - - 0 1"), TERM_PAWNS);
    CHECK(isolated.mg < connected.mg);
    CHECK(isolated.eg < connected.eg);
}

TEST(eval_supported_pawns_get_a_bonus_that_grows_with_rank) {
    const int low = white(trace("4k3/8/8/8/8/2P5/1P6/4K3 w - - 0 1"), TERM_PAWNS).mg;   // c3 supported by b2
    const int high = white(trace("4k3/8/2P5/1P6/8/8/8/4K3 w - - 0 1"), TERM_PAWNS).mg;  // c6 supported by b5
    CHECK(high > low);
}

TEST(eval_passed_pawns_count_and_grow_with_rank) {
    const TermScore passed = white(trace("4k3/8/8/4P3/8/8/8/4K3 w - - 0 1"), TERM_PASSED);
    CHECK(passed.mg > 0 && passed.eg > 0);
    // A black pawn on d6 (it can capture on e5) means the e-pawn is not passed.
    const TermScore guarded = white(trace("4k3/8/3p4/4P3/8/8/8/4K3 w - - 0 1"), TERM_PASSED);
    CHECK_EQ(guarded.mg, 0);
    CHECK_EQ(guarded.eg, 0);
    const TermScore advanced = white(trace("4k3/8/4P3/8/8/8/8/4K3 w - - 0 1"), TERM_PASSED);
    CHECK(advanced.mg > passed.mg && advanced.eg > passed.eg);
    // A blocked passed pawn is worth less than a free one.
    const TermScore blocked = white(trace("8/8/8/4Pn2/8/8/8/k3K3 w - - 0 1"), TERM_PASSED);   // the knight stands on f5, not in front
    const TermScore blockedFront = white(trace("8/8/4n3/4P3/8/8/8/k3K3 w - - 0 1"), TERM_PASSED);
    CHECK(blockedFront.mg < blocked.mg);
}

TEST(eval_passed_pawn_likes_the_own_king_close_and_the_enemy_king_far) {
    const int near = white(trace("8/8/8/4P3/4K3/8/8/k7 w - - 0 1"), TERM_PASSED).eg;
    const int far = white(trace("8/8/8/4P3/8/8/8/k6K w - - 0 1"), TERM_PASSED).eg;
    CHECK(near != far);
    const int enemyNear = white(trace("8/3k4/8/4P3/8/8/8/K7 w - - 0 1"), TERM_PASSED).eg;
    const int enemyFar = white(trace("8/8/8/4P3/8/8/8/K6k w - - 0 1"), TERM_PASSED).eg;
    CHECK(enemyFar > enemyNear);
}

TEST(eval_bishop_pair) {
    CHECK_EQ(white(trace("4k3/8/8/8/8/8/8/2B1KB2 w - - 0 1"), TERM_IMBALANCE).mg, 30);
    CHECK_EQ(white(trace("4k3/8/8/8/8/8/8/2B1K3 w - - 0 1"), TERM_IMBALANCE).mg, 0);
    CHECK_EQ(black(trace("2b1kb2/8/8/8/8/8/8/4K3 w - - 0 1"), TERM_IMBALANCE).eg, 50);
}

TEST(eval_rooks_like_open_files_and_the_seventh) {
    const int closed = white(trace("4k3/8/8/8/8/8/4P3/4RK2 w - - 0 1"), TERM_ROOKS).mg;     // own pawn on the e-file
    const int semi = white(trace("4k3/4p3/8/8/8/8/8/4RK2 w - - 0 1"), TERM_ROOKS).mg;      // only an enemy pawn
    const int open = white(trace("7k/8/8/8/8/8/8/4RK2 w - - 0 1"), TERM_ROOKS).mg;         // no pawns
    CHECK(open > semi);
    CHECK(semi > closed);
    CHECK(white(trace("7k/4R3/8/8/8/8/8/5K2 w - - 0 1"), TERM_ROOKS).eg > white(trace("7k/8/8/8/8/8/4R3/5K2 w - - 0 1"), TERM_ROOKS).eg);
}

TEST(eval_mobility_prefers_active_pieces) {
    CHECK(white(trace("4k3/8/8/8/3N4/8/8/4K3 w - - 0 1"), TERM_MOBILITY).mg > white(trace("4k3/8/8/8/8/8/8/N3K3 w - - 0 1"), TERM_MOBILITY).mg);
    // A rook shut in by its own pieces is worse than a free one.
    CHECK(white(trace("4k3/8/8/8/8/8/8/R6K w - - 0 1"), TERM_MOBILITY).mg > white(trace("4k3/8/8/8/8/8/P7/RK6 w - - 0 1"), TERM_MOBILITY).mg);
    // Squares attacked by enemy pawns do not count.
    CHECK(white(trace("4k3/8/8/8/3N4/8/8/4K3 w - - 0 1"), TERM_MOBILITY).mg > white(trace("4k3/8/2p1p3/8/3N4/8/8/4K3 w - - 0 1"), TERM_MOBILITY).mg);
}

TEST(eval_knight_outpost) {
    // Nd5 supported by c4; no black pawn can ever attack d5.
    CHECK_EQ(white(trace("4k3/8/8/3N4/2P5/8/8/4K3 w - - 0 1"), TERM_KNIGHTS).mg, 22);
    // A black pawn on e7 can still advance to e6 and chase the knight: no outpost.
    CHECK_EQ(white(trace("4k3/4p3/8/3N4/2P5/8/8/4K3 w - - 0 1"), TERM_KNIGHTS).mg, 0);
    // Unsupported: no outpost.
    CHECK_EQ(white(trace("4k3/8/8/3N4/8/8/8/4K3 w - - 0 1"), TERM_KNIGHTS).mg, 0);
}

TEST(eval_king_safety_pawn_shield_and_attackers) {
    const int shielded = white(trace("4k3/8/8/8/8/8/5PPP/6K1 w - - 0 1"), TERM_KING).mg;
    const int bare = white(trace("4k3/8/8/8/8/8/8/6K1 w - - 0 1"), TERM_KING).mg;
    CHECK(shielded > bare);
    // Open file next to the king is worse than a closed one (other pawns equal).
    const int closedFile = white(trace("4k3/5p2/8/8/8/8/5PPP/6K1 w - - 0 1"), TERM_KING).mg;
    const int openFile = white(trace("4k3/8/8/8/8/8/6PP/6K1 w - - 0 1"), TERM_KING).mg;
    CHECK(closedFile > openFile);
    // A queen and a knight bearing down on the king zone cost safety.
    const int calm = white(trace("4k3/8/8/8/8/8/5PPP/6K1 w - - 0 1"), TERM_KING).mg;
    const int attacked = white(trace("4k3/8/8/8/8/5n2/5PPP/6K1 w - - 0 1"), TERM_KING).mg;
    const int attackedMore = white(trace("4k3/8/8/6q1/8/5n2/5PPP/6K1 w - - 0 1"), TERM_KING).mg;
    CHECK(attacked <= calm);
    CHECK(attackedMore < calm);
}

TEST(eval_mopup_drives_the_lone_king_to_the_edge) {
    // K+Q v K: the closer the kings and the farther the lone king from the center, the better.
    const int centered = white(trace("8/8/8/3k4/8/8/8/1Q2K3 w - - 0 1"), TERM_ENDGAME).eg;
    const int cornered = white(trace("k7/8/8/8/8/8/8/1Q2K3 w - - 0 1"), TERM_ENDGAME).eg;
    CHECK(cornered > centered);
    const int close = white(trace("k7/8/1K6/8/8/8/8/1Q6 w - - 0 1"), TERM_ENDGAME).eg;
    CHECK(close > cornered);
    // Not applicable when the opponent still has pawns.
    CHECK_EQ(white(trace("k7/p7/8/8/8/8/8/1Q2K3 w - - 0 1"), TERM_ENDGAME).eg, 0);
    // Not applicable with too little material (lone knight).
    CHECK_EQ(white(trace("k7/8/8/8/8/8/8/N3K3 w - - 0 1"), TERM_ENDGAME).eg, 0);
}

TEST(eval_unstoppable_passed_pawn_rule_of_the_square) {
    // The black king is far away: the e-pawn runs through whoever moves.
    CHECK_EQ(white(trace("k7/8/8/4P3/8/8/8/4K3 w - - 0 1"), TERM_ENDGAME).eg, 450);
    CHECK_EQ(white(trace("k7/8/8/4P3/8/8/8/4K3 b - - 0 1"), TERM_ENDGAME).eg, 450);
    // The black king stands inside the square and is close enough to catch it.
    CHECK_EQ(white(trace("8/3k4/8/4P3/8/8/8/4K3 w - - 0 1"), TERM_ENDGAME).eg, 0);
    // Tempo matters at the border: pawn on e5 needs 3 moves, a king on b8 needs 3 moves to reach e8.
    CHECK_EQ(white(trace("1k6/8/8/4P3/8/8/8/4K3 w - - 0 1"), TERM_ENDGAME).eg, 450);  // pawn moves first: it wins the race
    CHECK_EQ(white(trace("1k6/8/8/4P3/8/8/8/4K3 b - - 0 1"), TERM_ENDGAME).eg, 0);    // king moves first: catches it
    // Never when the opponent still has a piece that could stop it.
    CHECK_EQ(white(trace("kn6/8/8/4P3/8/8/8/4K3 w - - 0 1"), TERM_ENDGAME).eg, 0);
}

TEST(eval_opposite_colored_bishops_are_drawish) {
    // c1 is a dark square, d3 a light one.
    const EvalTrace ocb = trace("4k3/p7/8/8/8/3b4/P7/K1B5 w - - 0 1");
    CHECK_EQ(ocb.egScale, 64);
    const EvalTrace same = trace("4k3/p7/8/8/8/2b5/P7/K1B5 w - - 0 1");   // both on dark squares
    CHECK_EQ(same.egScale, 128);
    // Other pieces on the board: no scaling.
    CHECK_EQ(trace("4k3/p7/8/8/8/3b4/P7/K1B4R w - - 0 1").egScale, 128);
}

TEST(eval_trace_formatting) {
    const std::string s = format_trace(trace(Position::START_FEN));
    CHECK(s.find("Pawn structure") != std::string::npos);
    CHECK(s.find("King safety") != std::string::npos);
    CHECK(s.find("Final evaluation: +0.00") != std::string::npos);
    CHECK(s.find("Phase 24/24") != std::string::npos);
}
