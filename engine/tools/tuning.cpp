#include "tuning.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <thread>

#include "eval/evaluation.h"
#include "eval/params.h"

namespace albatross::tuning {

bool load_dataset(const std::string& path, Dataset& out, std::size_t maxEntries, std::size_t* skipped, std::string* error) {
    std::ifstream in(path);
    if (!in) {
        if (error) *error = "cannot open " + path;
        return false;
    }
    std::size_t bad = 0;
    for (std::string line; std::getline(in, line);) {
        if (maxEntries && out.entries.size() >= maxEntries) break;
        const std::size_t bar = line.rfind('|');
        if (bar == std::string::npos) { ++bad; continue; }
        const std::string fen = line.substr(0, bar);
        char* end = nullptr;
        const std::string res = line.substr(bar + 1);
        const double result = std::strtod(res.c_str(), &end);
        if (end == res.c_str() || (result != 0.0 && result != 0.5 && result != 1.0)) { ++bad; continue; }
        Entry e;
        if (!e.pos.set(fen)) { ++bad; continue; }
        e.pos.compact();
        e.result = result;
        out.entries.push_back(std::move(e));
    }
    if (skipped) *skipped = bad;
    return true;
}

void split_dataset(Dataset& all, int every, Dataset& training, Dataset& validation) {
    for (std::size_t i = 0; i < all.entries.size(); ++i)
        (every > 0 && int(i % std::size_t(every)) == 0 ? validation : training).entries.push_back(std::move(all.entries[i]));
    all.entries.clear();
}

double expected_result(double evalCp, double K) { return 1.0 / (1.0 + std::pow(10.0, -K * evalCp / 400.0)); }

double loss(const Dataset& data, double K, int threads) {
    const std::size_t n = data.entries.size();
    if (n == 0) return 0.0;
    threads = std::max(1, std::min<int>(threads, int(n)));
    std::vector<double> partial(std::size_t(threads), 0.0);
    auto work = [&](int t) {
        double sum = 0;
        for (std::size_t i = std::size_t(t); i < n; i += std::size_t(threads)) {
            const Entry& e = data.entries[i];
            const int stm = evaluate(e.pos);
            const double white = e.pos.side_to_move() == WHITE ? stm : -stm;
            const double d = e.result - expected_result(white, K);
            sum += d * d;
        }
        partial[std::size_t(t)] = sum;
    };
    if (threads == 1) work(0);
    else {
        std::vector<std::thread> pool;
        for (int t = 0; t < threads; ++t) pool.emplace_back(work, t);
        for (auto& th : pool) th.join();
    }
    double total = 0;
    for (double p : partial) total += p;
    return total / double(n);
}

double fit_k(const Dataset& data, int threads) {
    // The loss is smooth with a single minimum in K: golden-section search.
    const double phi = (std::sqrt(5.0) - 1) / 2;
    double a = 0.05, b = 4.0;
    double c = b - phi * (b - a), d = a + phi * (b - a);
    double fc = loss(data, c, threads), fd = loss(data, d, threads);
    for (int i = 0; i < 40; ++i) {
        if (fc < fd) { b = d; d = c; fd = fc; c = b - phi * (b - a); fc = loss(data, c, threads); }
        else { a = c; c = d; fc = fd; d = a + phi * (b - a); fd = loss(data, d, threads); }
    }
    return (a + b) / 2;
}

bool is_tunable(const std::string& name) {
    // Split "pstPawnMg[12]" into base name and index.
    std::string base = name;
    int index = -1;
    const std::size_t br = name.find('[');
    if (br != std::string::npos) {
        base = name.substr(0, br);
        index = std::atoi(name.c_str() + br + 1);
    }
    auto in = [&](std::initializer_list<int> v) { return std::find(v.begin(), v.end(), index) != v.end(); };

    if (base == "materialMg") return !in({0, 1, 6});          // 1 = the pawn: anchor of the whole scale
    if (base == "materialEg") return !in({0, 6});
    if (base == "kingAttackUnit") return false;               // enters the score quadratically
    if (base == "mobilityBase") return false;                 // redundant with the weights and the tables
    if (base == "mobilityMg" || base == "mobilityEg") return !in({0, 1, 6});
    if (base == "pstPawnMg" || base == "pstPawnEg") return index >= 8 && index < 56;   // no pawns on rank 1 or 8
    if (base == "supportedPawnMg" || base == "phalanxPawnMg") return !in({0, 1, 7});
    if (base == "passedMg" || base == "passedEg") return !in({0, 7});
    return true;
}

Result tune(const Dataset& training, const Dataset& validation, double K, const Options& opt, const Log& log) {
    std::vector<int*> params;
    for (const TunableParam& p : tunable_params())
        if (is_tunable(p.name)) params.push_back(p.value);

    Result r;
    r.k = K;
    const std::vector<int> start = params_snapshot();
    double best = loss(training, K, opt.threads);
    r.trainBefore = best;
    r.validationBefore = loss(validation, K, opt.threads);
    double bestValidation = r.validationBefore;
    r.best = params_snapshot();
    log("tuning " + std::to_string(params.size()) + " parameters on " + std::to_string(training.entries.size()) +
        " positions (validation " + std::to_string(validation.entries.size()) + "), K = " + std::to_string(K) +
        ", loss " + std::to_string(best) + " / validation " + std::to_string(r.validationBefore));

    int sinceImprovement = 0;
    for (int pass = 1; pass <= opt.passes; ++pass) {
        int accepted = 0;
        for (int* p : params) {
            const int old = *p;
            for (int dir : {+1, -1}) {
                bool moved = false;
                // Keep stepping in a direction as long as it helps.
                for (;;) {
                    *p += dir * opt.step;
                    const double l = loss(training, K, opt.threads);
                    if (l < best - 1e-12) { best = l; moved = true; ++accepted; }
                    else { *p -= dir * opt.step; break; }
                }
                if (moved) break;   // do not try the other direction
            }
            (void)old;
        }
        const double v = loss(validation, K, opt.threads);
        r.passes = pass;
        log("pass " + std::to_string(pass) + ": " + std::to_string(accepted) + " changes, training loss " + std::to_string(best) +
            ", validation loss " + std::to_string(v));
        if (v < bestValidation - 1e-12) {
            bestValidation = v;
            r.best = params_snapshot();
            sinceImprovement = 0;
        } else if (++sinceImprovement >= opt.patience) {
            log("validation loss stopped improving; keeping the best snapshot");
            break;
        }
        if (accepted == 0) break;
    }
    params_restore(r.best);
    r.trainAfter = loss(training, K, opt.threads);
    r.validationAfter = loss(validation, K, opt.threads);
    for (std::size_t i = 0; i < start.size(); ++i) r.changed += start[i] != r.best[i];
    return r;
}

std::string format_params() {
    std::ostringstream os;
#define SCORE(name, mgValue, egValue) os << #name << " = " << EvalP.name.mg << " " << EvalP.name.eg << "\n";
#define INT(name, value) os << #name << " = " << EvalP.name << "\n";
#define ARRAY(name, size, ...)            \
    os << #name << " =";                  \
    for (int i = 0; i < size; ++i) os << " " << EvalP.name[i]; \
    os << "\n";
#include "eval/params.def"
#undef SCORE
#undef INT
#undef ARRAY
    return os.str();
}

}  // namespace albatross::tuning
