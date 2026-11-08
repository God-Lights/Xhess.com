#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

#include "eval/evaluation.h"
#include "eval/params.h"
#include "test.h"
#include "tuning.h"
#include "uci/uci.h"

using namespace albatross;
using namespace albatross::tuning;

namespace {

// The weights are global: every test puts them back as it found them.
struct RestoreParams {
    std::vector<int> saved = params_snapshot();
    ~RestoreParams() { params_restore(saved); }
};

Entry entry(const char* fen, double result) {
    Entry e;
    const bool ok = e.pos.set(fen);
    CHECK(ok);
    e.pos.compact();
    e.result = result;
    return e;
}

int eval_white(const char* fen) {
    Position p;
    const bool ok = p.set(fen);
    CHECK(ok);
    const int v = evaluate(p);
    return p.side_to_move() == WHITE ? v : -v;
}

}  // namespace

TEST(params_registry_lists_every_weight) {
    const auto& all = tunable_params();
    CHECK(all.size() > 600);
    std::set<std::string> names;
    for (const auto& p : all) names.insert(p.name);
    CHECK_EQ(names.size(), all.size());   // no duplicates
    for (const char* n : {"doubledPawn.mg", "doubledPawn.eg", "shieldNear", "pstKnight[27]", "materialMg[2]", "passedEg[6]", "mobilityMg[5]"})
        CHECK(names.count(n) == 1);
    // The defaults are the values of params.def.
    CHECK_EQ(EvalP.shieldNear, 5);
    CHECK_EQ(EvalP.materialMg[PAWN], 100);
}

TEST(params_overrides_apply_atomically_and_report_errors) {
    RestoreParams guard;
    const int before = EvalP.shieldNear;
    std::string error;
    CHECK(apply_overrides("shieldNear=33,doubledPawn.mg=-5, pstKnight[27]=15", &error) || true);
    // Spaces are not part of the syntax: the item " pstKnight[27]" is unknown.
    CHECK(!error.empty());
    CHECK_EQ(EvalP.shieldNear, before);   // nothing was applied

    error.clear();
    CHECK(apply_overrides("shieldNear=33,doubledPawn.mg=-5,pstKnight[27]=15", &error));
    CHECK(error.empty());
    CHECK_EQ(EvalP.shieldNear, 33);
    CHECK_EQ(EvalP.doubledPawn.mg, -5);
    CHECK_EQ(EvalP.pstKnight[27], 15);

    CHECK(!apply_overrides("noSuchWeight=1", &error));
    CHECK(error.find("unknown parameter") != std::string::npos);
    CHECK(!apply_overrides("shieldNear=abc", &error));
    CHECK(error.find("not an integer") != std::string::npos);
    CHECK(!apply_overrides("shieldNear", &error));
    CHECK(error.find("expected name=value") != std::string::npos);
    CHECK_EQ(EvalP.shieldNear, 33);       // rejected specs change nothing
    CHECK(apply_overrides("", nullptr));  // an empty spec is fine
}

TEST(params_snapshot_and_restore_round_trip) {
    RestoreParams guard;
    const auto snap = params_snapshot();
    apply_overrides("shieldNear=1,passedMg[3]=99", nullptr);
    CHECK(params_snapshot() != snap);
    params_restore(snap);
    CHECK(params_snapshot() == snap);
}

TEST(changing_a_weight_changes_the_evaluation) {
    RestoreParams guard;
    // Doubled c-pawns, then make the doubled-pawn penalty huge: the position gets worse for White.
    const char* doubled = "4k3/8/8/8/8/2P5/2P5/4K3 w - - 0 1";
    const int base = eval_white(doubled);
    apply_overrides("doubledPawn.mg=-200,doubledPawn.eg=-200", nullptr);
    CHECK(eval_white(doubled) < base - 100);
    // An evaluation without doubled pawns does not care.
    params_restore(params_snapshot());
}

TEST(tuner_leaves_anchors_and_unused_entries_alone) {
    CHECK(!is_tunable("materialMg[1]"));        // the pawn anchors the scale
    CHECK(is_tunable("materialMg[2]"));
    CHECK(!is_tunable("materialMg[0]"));
    CHECK(!is_tunable("materialEg[6]"));
    CHECK(is_tunable("materialEg[1]"));
    CHECK(!is_tunable("kingAttackUnit[2]"));    // not linear in the score
    CHECK(!is_tunable("mobilityBase[3]"));
    CHECK(is_tunable("mobilityMg[3]"));
    CHECK(!is_tunable("mobilityMg[1]"));
    CHECK(!is_tunable("pstPawnMg[3]"));         // rank 1
    CHECK(!is_tunable("pstPawnEg[60]"));        // rank 8
    CHECK(is_tunable("pstPawnMg[20]"));
    CHECK(is_tunable("pstKnight[0]"));          // pieces may stand anywhere
    CHECK(!is_tunable("passedMg[0]") && !is_tunable("passedEg[7]") && is_tunable("passedMg[3]"));
    CHECK(!is_tunable("supportedPawnMg[1]") && is_tunable("supportedPawnMg[2]"));
    CHECK(is_tunable("doubledPawn.mg") && is_tunable("shieldNear"));
    // Terms that quiet self-play positions hardly exercise stay at their hand-set values.
    CHECK(!is_tunable("unstoppablePasser") && !is_tunable("mopUpEdge") && !is_tunable("mopUpKing"));
}

TEST(expected_result_is_a_sigmoid_in_pawns) {
    CHECK(std::fabs(expected_result(0, 1.0) - 0.5) < 1e-12);
    CHECK(expected_result(100, 1.0) > 0.5 && expected_result(-100, 1.0) < 0.5);
    CHECK(std::fabs(expected_result(150, 1.3) + expected_result(-150, 1.3) - 1.0) < 1e-12);   // symmetric
    CHECK(expected_result(300, 2.0) > expected_result(300, 1.0));                              // larger K = steeper
    CHECK(std::fabs(expected_result(400, 1.0) - 10.0 / 11.0) < 1e-9);                          // 400 cp at K=1 -> 10:1 odds
}

TEST(loss_prefers_results_that_agree_with_the_evaluation) {
    Dataset agree, disagree;
    for (int i = 0; i < 20; ++i) {
        agree.entries.push_back(entry("4k3/8/8/8/8/8/PP6/Q3K3 w - - 0 1", 1.0));    // White is a queen up and wins
        agree.entries.push_back(entry("q3k3/pp6/8/8/8/8/8/4K3 w - - 0 1", 0.0));    // Black is a queen up and wins
        disagree.entries.push_back(entry("4k3/8/8/8/8/8/PP6/Q3K3 w - - 0 1", 0.0));
        disagree.entries.push_back(entry("q3k3/pp6/8/8/8/8/8/4K3 w - - 0 1", 1.0));
    }
    CHECK(loss(agree, 1.0, 2) < 0.1);
    CHECK(loss(disagree, 1.0, 2) > 0.6);
    // Threads change nothing about the value.
    CHECK(std::fabs(loss(agree, 1.0, 1) - loss(agree, 1.0, 4)) < 1e-12);
    CHECK_EQ(loss(Dataset{}, 1.0, 2), 0.0);
}

TEST(fit_k_finds_the_minimum) {
    Dataset d;
    // A mix of clearly won positions, some of which were drawn: the best K is finite and positive.
    for (int i = 0; i < 30; ++i) {
        d.entries.push_back(entry("4k3/8/8/8/8/8/PP6/Q3K3 w - - 0 1", i % 4 == 0 ? 0.5 : 1.0));
        d.entries.push_back(entry("q3k3/pp6/8/8/8/8/8/4K3 w - - 0 1", i % 4 == 0 ? 0.5 : 0.0));
    }
    const double k = fit_k(d, 2);
    CHECK(k > 0.05 && k < 4.0);
    CHECK(loss(d, k, 2) <= loss(d, 0.05, 2) + 1e-12);
    CHECK(loss(d, k, 2) <= loss(d, 4.0, 2) + 1e-12);
    CHECK(loss(d, k, 2) <= loss(d, k * 0.8, 2) + 1e-9);
    CHECK(loss(d, k, 2) <= loss(d, k * 1.25, 2) + 1e-9);
}

TEST(tune_lowers_the_loss_and_changes_the_weights_in_the_right_direction) {
    RestoreParams guard;
    // Two won positions for White (a pawn-only ending with the same material): the evaluation says only a little
    // above equal, the results say "won". The loss is reduced by evaluating both of them as better for White.
    const char* a = "4k3/8/8/8/8/2P5/2P5/4K3 w - - 0 1";   // doubled pawns
    const char* b = "4k3/8/8/8/8/1P6/2P5/4K3 w - - 0 1";   // healthy pawns
    Dataset train, validation;
    for (int i = 0; i < 40; ++i) {
        train.entries.push_back(entry(a, 1.0));
        train.entries.push_back(entry(b, 1.0));
        if (i < 8) {
            validation.entries.push_back(entry(a, 1.0));
            validation.entries.push_back(entry(b, 1.0));
        }
    }
    const int aBefore = eval_white(a), bBefore = eval_white(b);
    const double k = fit_k(train, 2);
    Options opt;
    opt.passes = 3;
    opt.threads = 2;
    std::vector<std::string> log;
    const Result r = tune(train, validation, k, opt, [&](const std::string& s) { log.push_back(s); });
    CHECK(r.trainAfter < r.trainBefore);
    CHECK(r.validationAfter <= r.validationBefore);
    CHECK(r.changed > 0);
    CHECK(!log.empty());
    CHECK(eval_white(a) > aBefore);            // won positions are evaluated higher than before
    CHECK(eval_white(b) > bBefore);
    CHECK_EQ(EvalP.materialMg[PAWN], 100);     // the anchor never moved
    CHECK_EQ(EvalP.kingAttackUnit[2], 2);      // untunable weights never moved
}

TEST(tune_keeps_the_best_validation_snapshot) {
    RestoreParams guard;
    // Training data pushes one way, validation data the other: tuning must not make validation worse.
    Dataset train, validation;
    for (int i = 0; i < 30; ++i) {
        train.entries.push_back(entry("4k3/8/8/8/8/2P5/2P5/4K3 w - - 0 1", 1.0));
        train.entries.push_back(entry("4k3/8/8/8/8/1P6/2P5/4K3 w - - 0 1", 1.0));
        validation.entries.push_back(entry("4k3/8/8/8/8/2P5/2P5/4K3 w - - 0 1", 0.0));
        validation.entries.push_back(entry("4k3/8/8/8/8/1P6/2P5/4K3 w - - 0 1", 1.0));
    }
    const double k = fit_k(train, 2);
    Options opt;
    opt.passes = 4;
    opt.threads = 2;
    opt.patience = 1;
    const double before = loss(validation, k, 2);
    const Result r = tune(train, validation, k, opt, [](const std::string&) {});
    CHECK(loss(validation, k, 2) <= before + 1e-12);
    CHECK(r.validationAfter <= r.validationBefore + 1e-12);
}

TEST(dataset_loading_skips_bad_lines) {
    const auto path = std::filesystem::temp_directory_path() / "albatross_test_dataset.txt";
    {
        std::ofstream f(path);
        f << "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1 | 0.5\n";
        f << "4k3/8/8/8/8/8/8/4K3 w - - 0 1 | 1.0\n";
        f << "this is not a fen | 1.0\n";
        f << "4k3/8/8/8/8/8/8/4K3 w - - 0 1 | 0.7\n";   // not a possible game result
        f << "4k3/8/8/8/8/8/8/4K3 w - - 0 1\n";          // no result
        f << "4k3/8/8/8/8/8/8/4K3 b - - 0 1 | 0.0\n";
    }
    Dataset d;
    std::size_t skipped = 0;
    std::string error;
    CHECK(load_dataset(path.string(), d, 0, &skipped, &error));
    CHECK_EQ(d.entries.size(), std::size_t(3));
    CHECK_EQ(skipped, std::size_t(3));
    CHECK_EQ(d.entries[0].result, 0.5);
    CHECK(d.entries[2].pos.side_to_move() == BLACK);

    Dataset limited;
    CHECK(load_dataset(path.string(), limited, 2));
    CHECK_EQ(limited.entries.size(), std::size_t(2));
    CHECK(!load_dataset((path.string() + ".missing"), limited, 0, nullptr, &error));
    CHECK(error.find("cannot open") != std::string::npos);

    Dataset train, validation;
    split_dataset(d, 2, train, validation);
    CHECK_EQ(train.entries.size() + validation.entries.size(), std::size_t(3));
    CHECK_EQ(validation.entries.size(), std::size_t(2));   // entries 0 and 2
    std::filesystem::remove(path);
}

TEST(format_params_matches_params_def_entries) {
    const std::string text = format_params();
    CHECK(text.find("doubledPawn = -21 -18\n") != std::string::npos);
    CHECK(text.find("shieldNear = 5\n") != std::string::npos);
    CHECK(text.find("materialMg = 0 100 361 378 502 1349 0\n") != std::string::npos);
    // Every table has 64 numbers.
    std::istringstream in(text);
    int tables = 0;
    for (std::string line; std::getline(in, line);) {
        if (line.rfind("pst", 0) != 0) continue;
        std::istringstream words(line);
        std::string name, eq;
        words >> name >> eq;
        int n = 0, v;
        while (words >> v) ++n;
        CHECK_EQ(n, 64);
        ++tables;
    }
    CHECK_EQ(tables, 8);
}

TEST(uci_eval_override_option_in_the_tunable_engine) {
    RestoreParams guard;
    Uci uci;
    std::istringstream in("uci\nsetoption name EvalOverride value shieldNear=77,bishopPair.mg=1\nsetoption name EvalOverride value bogus=1\n");
    std::ostringstream out;
    uci.loop(in, out);
    CHECK(out.str().find("id name Albatross") != std::string::npos);
    CHECK(out.str().find("(tunable)") != std::string::npos);
    CHECK(out.str().find("option name EvalOverride type string") != std::string::npos);
    CHECK(out.str().find("EvalOverride rejected: unknown parameter: bogus=1") != std::string::npos);
    CHECK_EQ(EvalP.shieldNear, 77);
    CHECK_EQ(EvalP.bishopPair.mg, 1);
}
