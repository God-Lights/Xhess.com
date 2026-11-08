#pragma once
// Texel tuning of the evaluation weights.
//
// Idea: a good evaluation predicts who wins. For positions taken from self-play games (labelled with the result of
// the whole game) the score e is turned into an expected result  s(e) = 1 / (1 + 10^(-K e / 400))  and the tuner
// minimises the mean squared error  (result - s(e))^2  by changing one weight at a time (local search).
// Built with the tunable evaluation (-DALBATROSS_TUNABLE), so the weights can be modified in place.

#include <functional>
#include <string>
#include <vector>

#include "core/position.h"

namespace albatross::tuning {

struct Entry {
    Position pos;
    double result;   // White's point of view: 1, 0.5, 0
};

struct Dataset {
    std::vector<Entry> entries;
};

// Reads lines "<fen> | <result>". Malformed lines are skipped and counted in `skipped`. maxEntries 0 = all.
bool load_dataset(const std::string& path, Dataset& out, std::size_t maxEntries = 0, std::size_t* skipped = nullptr,
                  std::string* error = nullptr);

// Every `every`-th entry goes to `validation`, the rest to `training`.
void split_dataset(Dataset& all, int every, Dataset& training, Dataset& validation);

double expected_result(double evalCp, double K);

// Mean squared error of the current weights on a dataset (uses `threads` threads).
double loss(const Dataset& data, double K, int threads);

// The K that minimises the loss for the current weights (golden-section search).
double fit_k(const Dataset& data, int threads);

// Weights the tuner leaves alone: the pawn anchor, entries that never matter (empty piece slots, pawns on the
// first/last rank), and parameters that do not enter the score linearly (king attack units).
bool is_tunable(const std::string& name);

struct Options {
    int passes = 6;
    int step = 1;
    int threads = 4;
    int patience = 2;   // stop when the validation loss has not improved for this many passes
};

struct Result {
    double k = 0;
    double trainBefore = 0, trainAfter = 0;
    double validationBefore = 0, validationAfter = 0;
    int passes = 0;
    int changed = 0;             // parameters that differ from the start
    std::vector<int> best;       // snapshot (params_snapshot order) with the best validation loss
};

using Log = std::function<void(const std::string&)>;

// Coordinate descent. On return the weights hold the best validation snapshot.
Result tune(const Dataset& training, const Dataset& validation, double K, const Options& options, const Log& log);

// Text for tools/apply_params.py: one "name = values..." line per params.def entry.
std::string format_params();

}  // namespace albatross::tuning
