#include <cstdint>
#include <string>
#include <vector>

#include "core/perft.h"
#include "test.h"

using namespace albatross;

namespace {

struct PerftCase {
    const char* name;
    const char* fen;
    std::vector<std::uint64_t> nodes;  // nodes[d-1] = perft(d)
};

// Reference values from the Chess Programming Wiki "Perft Results" page.
const std::vector<PerftCase> Suite = {
    {"startpos", Position::START_FEN, {20, 400, 8902, 197281, 4865609, 119060324}},
    {"kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
     {48, 2039, 97862, 4085603, 193690690}},
    {"position3", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", {14, 191, 2812, 43238, 674624, 11030083}},
    {"position4", "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
     {6, 264, 9467, 422333, 15833292}},
    {"position5", "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", {44, 1486, 62379, 2103487, 89941194}},
    {"position6", "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
     {46, 2079, 89890, 3894594, 164075551}},
};

void run_suite(std::size_t maxDepth, std::size_t minDepth) {
    for (const auto& c : Suite) {
        Position p;
        CHECK(p.set(c.fen));
        for (std::size_t d = minDepth; d <= maxDepth && d <= c.nodes.size(); ++d) {
            const std::uint64_t got = perft(p, int(d));
            if (got != c.nodes[d - 1]) std::cerr << "  " << c.name << " depth " << d << "\n";
            CHECK_EQ(got, c.nodes[d - 1]);
        }
        CHECK_EQ(p.fen(), std::string(c.fen));  // perft must leave the position untouched
        CHECK(p.is_ok());
    }
}

}  // namespace

// Fast enough for every commit (~10M nodes).
TEST(perft_suite_shallow) { run_suite(4, 1); }

// Startpos depth 5 and the remaining depth-5 positions where affordable.
TEST(perft_startpos_5) {
    Position p;
    CHECK_EQ(perft(p, 5), std::uint64_t(4865609));
}

TEST(perft_divide_sums_to_total) {
    Position p;
    std::ostringstream os;
    CHECK_EQ(perft_divide(p, 3, &os), std::uint64_t(8902));
}

// Deep runs: `albatross_tests --slow` (CTest: -DALBATROSS_SLOW_TESTS=ON).
TEST_SLOW(perft_suite_deep) { run_suite(6, 5); }
