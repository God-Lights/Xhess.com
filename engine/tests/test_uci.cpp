#include <sstream>
#include <string>

#include "test.h"
#include "uci/uci.h"

using namespace albatross;

namespace {
std::string run(const std::string& script) {
    Uci uci;
    std::istringstream in(script);
    std::ostringstream out;
    uci.loop(in, out);
    return out.str();
}
bool contains(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }
}  // namespace

TEST(uci_handshake) {
    const std::string out = run("uci\nisready\nquit\n");
    CHECK(contains(out, "id name Albatross"));
    CHECK(contains(out, "uciok"));
    CHECK(contains(out, "readyok"));
}

TEST(uci_position_and_perft) {
    // After 1.e4 e5 2.Nf3 Black has 29 legal moves (14 pawn, 5 knight, 5 bishop, 4 queen, 1 king).
    const std::string out = run("position startpos moves e2e4 e7e5 g1f3\ngo perft 1\nquit\n");
    CHECK(contains(out, "Nodes searched: 29"));
    CHECK(contains(run("position startpos\ngo perft 3\n"), "Nodes searched: 8902"));
}

TEST(uci_position_fen) {
    const std::string out = run(
        "position fen r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1 moves e1g1\n"
        "d\nquit\n");
    CHECK(contains(out, "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R4RK1 b kq - 1 1"));
}

TEST(uci_rejects_illegal_input_and_keeps_state) {
    const std::string out = run("position startpos moves e2e4\nposition startpos moves e2e5\ngo perft 1\n");
    CHECK(contains(out, "illegal move: e2e5"));
    CHECK(contains(out, "Nodes searched: 20"));  // still the position after 1.e4 (20 replies)
    CHECK(contains(run("position fen garbage\n"), "illegal fen"));
}

TEST(uci_go_returns_a_legal_move) {
    CHECK(contains(run("position startpos\ngo depth 5\n"), "bestmove "));
    // Checkmated side has no move.
    CHECK(contains(run("position startpos moves f2f3 e7e5 g2g4 d8h4\ngo\n"), "bestmove 0000"));
}

TEST(uci_eval_command_prints_the_breakdown) {
    const std::string out = run("position startpos moves e2e4\neval\nquit\n");
    CHECK(contains(out, "Passed pawns"));
    CHECK(contains(out, "Final evaluation:"));
    CHECK(contains(out, "side to move"));
    CHECK(contains(run("uci\n"), "id name Albatross 0.5"));
}

TEST(uci_themes_command) {
    const std::string out = run("position fen k7/8/8/4P3/8/8/8/4K3 w - - 0 1\nthemes\nquit\n");
    CHECK(contains(out, "theme passed white e5 cp "));
    CHECK(contains(out, "themes done phase 0 scale 128 white "));
    // The standard start position has no themes at all except the (empty-handed) king shields? It must at least finish.
    CHECK(contains(run("position startpos\nthemes\nquit\n"), "themes done phase 24"));
}
