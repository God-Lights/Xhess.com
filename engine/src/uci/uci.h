#pragma once

#include <iosfwd>
#include <mutex>
#include <string>

#include "../core/position.h"
#include "../search/search.h"

// Largest Hash (MB) the UCI option accepts. The browser build is limited by WebAssembly memory.
#ifndef ALBATROSS_MAX_HASH_MB
#define ALBATROSS_MAX_HASH_MB 4096
#endif

namespace albatross {

// UCI front end. Reads commands from `in`, writes responses to `out`, so it can be unit-tested with stringstreams.
// "go" runs on a worker thread; every line written to `out` goes through one mutex.
class Uci {
public:
    Uci() = default;
    ~Uci();

    // Processes commands until "quit" or end of input. A finite search is allowed to finish when the
    // input ends (so scripted sessions get their bestmove); an infinite one is stopped.
    void loop(std::istream& in, std::ostream& out);

    // Returns false when the engine should quit.
    bool execute(const std::string& line, std::ostream& out);

    static constexpr const char* NAME = "Albatross 0.4.1-position-sense";
    static constexpr const char* AUTHOR = "Xhess";

private:
    void cmd_setoption(std::istream& args);
    void cmd_position(std::istream& args);
    void cmd_go(std::istream& args);
    void emit(const std::string& line);
    void halt_search();

    Position pos_;
    Search search_;
    int multiPv_ = 1;
    std::ostream* out_ = nullptr;
    std::mutex outMutex_;
};

// "ongoing", "checkmate", "stalemate", "draw_insufficient", "draw_fifty" or "draw_repetition" (threefold).
std::string game_status(const Position& pos);

// "cp 23" or "mate 3" (negative = being mated), the UCI score notation.
std::string format_score(int score);

}  // namespace albatross
