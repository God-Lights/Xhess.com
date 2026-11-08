// albatross_tuner --data data.txt --out tuned.txt [--max 400000] [--passes 6] [--threads 4] [--step 1]
// Prints the tuned weights to --out (apply them with tools/apply_params.py).
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>

#include "core/init.h"
#include "tuning.h"

int main(int argc, char** argv) {
    using namespace albatross;
    init();
    std::string data = "data.txt", out = "tuned.txt";
    std::size_t maxEntries = 0;
    tuning::Options opt;
    if (argc == 3 && std::string(argv[1]) == "--dump-defaults") {   // the current weights in tuned.txt format
        std::ofstream(argv[2]) << tuning::format_params();
        return 0;
    }
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string a = argv[i];
        const char* v = argv[i + 1];
        if (a == "--data") data = v;
        else if (a == "--out") out = v;
        else if (a == "--max") maxEntries = std::strtoull(v, nullptr, 10);
        else if (a == "--passes") opt.passes = std::atoi(v);
        else if (a == "--threads") opt.threads = std::atoi(v);
        else if (a == "--step") opt.step = std::atoi(v);
        else { std::cerr << "unknown option " << a << "\n"; return 1; }
    }
    tuning::Dataset all, train, validation;
    std::size_t skipped = 0;
    std::string error;
    if (!tuning::load_dataset(data, all, maxEntries, &skipped, &error)) { std::cerr << error << "\n"; return 1; }
    std::cerr << "loaded " << all.entries.size() << " positions (" << skipped << " lines skipped)\n";
    tuning::split_dataset(all, 10, train, validation);

    const auto t0 = std::chrono::steady_clock::now();
    const double K = tuning::fit_k(train, opt.threads);
    const tuning::Result r = tuning::tune(train, validation, K, opt, [&](const std::string& s) {
        const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::cerr << "[" << int(sec) << " s] " << s << "\n";
    });
    std::ofstream(out) << tuning::format_params();
    std::cout << "K " << r.k << "\ntraining loss   " << r.trainBefore << " -> " << r.trainAfter
              << "\nvalidation loss " << r.validationBefore << " -> " << r.validationAfter << "  (" << r.passes << " passes, "
              << r.changed << " parameters changed)\nweights written to " << out << "\n";
    return 0;
}
