#include <atomic>
#include <chrono>
#include <iostream>
#include <string>

#include "core/init.h"
#include "core/perft.h"
#include "search/search.h"
#include "uci/uci.h"

int main(int argc, char** argv) {
    using namespace albatross;
    init();

    // `albatross perft <depth> [fen]` for quick command-line checks.
    if (argc >= 3 && std::string(argv[1]) == "perft") {
        Position pos;
        if (argc >= 4) {
            std::string fen;
            for (int i = 3; i < argc; ++i) fen += std::string(argv[i]) + " ";
            if (!pos.set(fen)) {
                std::cerr << "illegal fen\n";
                return 1;
            }
        }
        const auto t0 = std::chrono::steady_clock::now();
        const std::uint64_t nodes = perft_divide(pos, std::stoi(argv[2]), &std::cout);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::cout << "\nNodes: " << nodes << "  Time: " << ms << " ms  NPS: "
                  << (ms > 0 ? static_cast<std::uint64_t>(nodes / ms * 1000) : 0) << '\n';
        return 0;
    }

    // `albatross bench [depth]`: fixed-depth search over a set of positions. The node count is deterministic,
    // so it detects accidental search changes; NPS depends on the machine.
    if (argc >= 2 && std::string(argv[1]) == "bench") {
        static const char* Fens[] = {
            Position::START_FEN,
            "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
            "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
            "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
            "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
            "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
            "4k3/8/8/8/8/8/4P3/4K3 w - - 0 1",
        };
        Limits limits;
        limits.depth = argc >= 3 ? std::stoi(argv[2]) : 6;
        std::atomic<bool> stop{false};
        TranspositionTable tt(16);
        std::uint64_t total = 0;
        const auto t0 = std::chrono::steady_clock::now();
        for (const char* fen : Fens) {
            Position pos;
            pos.set(fen);
            tt.clear();  // every position starts from an empty table so node counts stay deterministic
            const SearchResult r = search_position(pos, limits, stop, tt);
            std::cout << to_uci(r.best) << " score " << format_score(r.score) << " nodes " << r.nodes << "  " << fen << '\n';
            total += r.nodes;
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::cout << "\nNodes searched: " << total << "  Time: " << ms << " ms  NPS: "
                  << (ms > 0 ? static_cast<std::uint64_t>(total / ms * 1000) : 0) << '\n';
        return 0;
    }

    Uci uci;
    uci.loop(std::cin, std::cout);
    return 0;
}
