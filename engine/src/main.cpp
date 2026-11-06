#include <chrono>
#include <iostream>
#include <string>

#include "core/init.h"
#include "core/perft.h"
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

    Uci uci;
    uci.loop(std::cin, std::cout);
    return 0;
}
