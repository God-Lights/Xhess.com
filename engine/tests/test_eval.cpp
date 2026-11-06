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
