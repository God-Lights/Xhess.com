#pragma once
// Self-play game generator for tuning. Plays Albatross against itself with a fixed node budget per move
// and records "quiet" positions together with the final result of the game.

#include <cstdint>
#include <string>
#include <vector>

namespace albatross::tools {

struct DatagenOptions {
    std::uint64_t nodes = 5000;   // search budget per move
    int openingPlies = 8;         // random moves that make the games different from each other
    int maxOpeningEval = 250;     // centipawns: a random opening that is already lopsided is thrown away
    int maxPlies = 300;           // longer games are drawn
    int sampleOneIn = 2;          // keep one quiet position out of this many
};

struct Sample {
    std::string fen;
    double result;   // of the whole game, from White's point of view: 1, 0.5 or 0
};

struct GameRecord {
    std::vector<Sample> samples;
    double result = 0.5;
    int plies = 0;
    std::string termination;   // checkmate, stalemate, draw_rule, win_adjudication, mate_found, draw_adjudication, max_plies
};

// Plays one game. The same seed gives the same game (single-threaded search, node-limited).
GameRecord play_game(std::uint64_t seed, const DatagenOptions& options = {});

}  // namespace albatross::tools
