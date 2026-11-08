#include <set>
#include <string>

#include "core/position.h"
#include "datagen.h"
#include "test.h"

using namespace albatross;
using namespace albatross::tools;

namespace {
DatagenOptions quick() {
    DatagenOptions o;
    o.nodes = 1500;
    o.sampleOneIn = 1;
    return o;
}
}  // namespace

TEST(datagen_is_deterministic_for_a_seed) {
    const GameRecord a = play_game(7, quick());
    const GameRecord b = play_game(7, quick());
    CHECK_EQ(a.plies, b.plies);
    CHECK_EQ(a.termination, b.termination);
    CHECK_EQ(a.result, b.result);
    CHECK_EQ(a.samples.size(), b.samples.size());
    for (std::size_t i = 0; i < a.samples.size() && i < b.samples.size(); ++i) CHECK_EQ(a.samples[i].fen, b.samples[i].fen);
    const GameRecord c = play_game(8, quick());
    CHECK(c.samples.empty() || a.samples.empty() || c.samples[0].fen != a.samples[0].fen);   // another seed, another game
}

TEST(datagen_samples_are_quiet_legal_positions_with_the_game_result) {
    const GameRecord g = play_game(3, quick());
    CHECK(!g.samples.empty());
    CHECK(g.result == 0.0 || g.result == 0.5 || g.result == 1.0);
    CHECK(!g.termination.empty());
    CHECK(g.plies > 0 && g.plies <= 300);
    std::set<std::string> seen;
    for (const Sample& s : g.samples) {
        Position p;
        CHECK(p.set(s.fen));
        CHECK(!p.in_check());               // quiet: the side to move is not in check
        CHECK_EQ(s.result, g.result);       // every sample carries the result of its game
        seen.insert(s.fen);
    }
    CHECK(seen.size() + 2 >= g.samples.size());   // (almost) no duplicates inside one game
}

TEST(datagen_games_start_from_different_random_openings_and_sampling_is_thinned) {
    // Openings differ between seeds.
    std::set<std::string> firsts;
    for (std::uint64_t seed = 1; seed <= 6; ++seed) {
        const GameRecord g = play_game(seed, quick());
        if (!g.samples.empty()) firsts.insert(g.samples.front().fen);
    }
    CHECK(firsts.size() >= 4);
    // Keeping one position in 4 gives clearly fewer samples than keeping every one.
    DatagenOptions thin = quick();
    thin.sampleOneIn = 4;
    const GameRecord all = play_game(11, quick());
    const GameRecord some = play_game(11, thin);
    CHECK(some.samples.size() < all.samples.size());
    CHECK_EQ(some.result, all.result);   // sampling does not change the game itself
}
