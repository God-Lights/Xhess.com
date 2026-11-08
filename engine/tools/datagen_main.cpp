// albatross_datagen --games 8000 --threads 4 --nodes 5000 --seed 1 --out data.txt
// Writes one line per position:  <fen> | <result>   (result from White's point of view: 1.0, 0.5, 0.0)
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/init.h"
#include "datagen.h"

int main(int argc, char** argv) {
    using namespace albatross;
    init();
    int games = 1000, threads = 4;
    std::uint64_t seed = 1;
    std::string out = "data.txt";
    tools::DatagenOptions opt;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string a = argv[i];
        const char* v = argv[i + 1];
        if (a == "--games") games = std::atoi(v);
        else if (a == "--threads") threads = std::atoi(v);
        else if (a == "--nodes") opt.nodes = std::strtoull(v, nullptr, 10);
        else if (a == "--seed") seed = std::strtoull(v, nullptr, 10);
        else if (a == "--out") out = v;
        else if (a == "--opening-plies") opt.openingPlies = std::atoi(v);
        else if (a == "--sample") opt.sampleOneIn = std::atoi(v);
        else { std::cerr << "unknown option " << a << "\n"; return 1; }
    }
    std::ofstream file(out);
    if (!file) { std::cerr << "cannot write " << out << "\n"; return 1; }

    std::atomic<int> next{0};
    std::mutex mutex;
    std::map<std::string, int> terminations;
    long positions = 0;
    int done = 0, white = 0, draws = 0, black = 0;
    const auto t0 = std::chrono::steady_clock::now();

    auto worker = [&] {
        for (int g; (g = next.fetch_add(1)) < games;) {
            const tools::GameRecord r = tools::play_game(seed * 1000003ULL + std::uint64_t(g), opt);
            std::lock_guard<std::mutex> lock(mutex);
            for (const tools::Sample& s : r.samples) {
                char res[8];
                std::snprintf(res, sizeof res, "%.1f", s.result);
                file << s.fen << " | " << res << '\n';
            }
            positions += long(r.samples.size());
            ++terminations[r.termination];
            (r.result == 1.0 ? white : r.result == 0.0 ? black : draws)++;
            if (++done % 200 == 0) {
                const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                std::cerr << done << "/" << games << " games, " << positions << " positions, " << int(s) << " s\n";
            }
        }
    };
    std::vector<std::thread> pool;
    for (int t = 0; t < threads; ++t) pool.emplace_back(worker);
    for (auto& t : pool) t.join();

    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::cout << games << " games in " << int(s) << " s: White " << white << ", draws " << draws << ", Black " << black
              << "; " << positions << " positions written to " << out << "\nterminations:";
    for (auto& [k, v] : terminations) std::cout << " " << k << "=" << v;
    std::cout << '\n';
    return 0;
}
