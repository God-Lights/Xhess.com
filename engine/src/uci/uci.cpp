#include "uci.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <vector>

#include "../core/movegen.h"
#include "../core/perft.h"

namespace albatross {

std::string format_score(int score) {
    if (is_mate_score(score)) {
        const int plies = MATE - (score > 0 ? score : -score);
        const int moves = (plies + 1) / 2;
        return "mate " + std::to_string(score > 0 ? moves : -moves);
    }
    return "cp " + std::to_string(score);
}

std::string game_status(const Position& pos) {
    MoveList moves;
    generate_legal(pos, moves);
    if (moves.empty()) return pos.in_check() ? "checkmate" : "stalemate";
    if (pos.insufficient_material()) return "draw_insufficient";
    if (pos.is_fifty_move_draw()) return "draw_fifty";
    if (pos.repetition_count() >= 3) return "draw_repetition";
    return "ongoing";
}

Uci::~Uci() { halt_search(); }

void Uci::emit(const std::string& line) {
    std::lock_guard<std::mutex> lock(outMutex_);
    if (out_) {
        *out_ << line << '\n';
        out_->flush();
    }
}

void Uci::halt_search() {
    search_.stop();
    search_.wait();
}

void Uci::loop(std::istream& in, std::ostream& out) {
    std::string line;
    while (std::getline(in, line)) {
        if (!execute(line, out)) return;
    }
    // End of input: let a finite search report its move, but never hang on an infinite one.
    if (search_.infinite()) search_.stop();
    search_.wait();
}

bool Uci::execute(const std::string& line, std::ostream& out) {
    out_ = &out;
    std::istringstream is(line);
    std::string cmd;
    if (!(is >> cmd)) return true;

    if (cmd == "uci") {
        emit(std::string("id name ") + NAME + "\nid author " + AUTHOR +
             "\noption name Hash type spin default 16 min 1 max 4096"
             "\noption name Clear Hash type button"
             "\noption name MultiPV type spin default 1 min 1 max 32"
             "\nuciok");
    } else if (cmd == "isready") {
        emit("readyok");
    } else if (cmd == "ucinewgame") {
        halt_search();
        search_.clear_hash();
        pos_.set(Position::START_FEN);
    } else if (cmd == "position") {
        halt_search();
        cmd_position(is);
    } else if (cmd == "go") {
        cmd_go(is);
    } else if (cmd == "stop") {
        search_.stop();
    } else if (cmd == "setoption") {
        halt_search();
        cmd_setoption(is);
    } else if (cmd == "d") {
        emit(pos_.pretty());
    } else if (cmd == "status") {
        // Non-standard debug command used by tools/match.py to judge games with the perft-verified rules.
        emit(std::string("status ") + game_status(pos_));
    } else if (cmd == "quit") {
        halt_search();
        return false;
    } else {
        emit("info string unknown command: " + cmd);
    }
    return true;
}

// "setoption name <id, may contain spaces> [value <x>]"
void Uci::cmd_setoption(std::istream& args) {
    std::string token, name, value;
    if (!(args >> token) || token != "name") return;
    while (args >> token && token != "value") name += (name.empty() ? "" : " ") + token;
    while (args >> token) value += (value.empty() ? "" : " ") + token;

    if (name == "Hash") {
        try {
            const long mb = std::stol(value);
            if (mb >= 1 && mb <= 4096) search_.set_hash_mb(std::size_t(mb));
            else emit("info string Hash must be between 1 and 4096 MB");
        } catch (const std::exception&) {
            emit("info string invalid Hash value: " + value);
        }
    } else if (name == "Clear Hash") {
        search_.clear_hash();
    } else if (name == "MultiPV") {
        try {
            multiPv_ = std::clamp(int(std::stol(value)), 1, 32);
        } catch (const std::exception&) {
            emit("info string invalid MultiPV value: " + value);
        }
    }
    // Other options (Threads, MultiPV, ...) arrive later; unknown names are ignored as the UCI spec asks.
}

void Uci::cmd_position(std::istream& args) {
    std::string token, fen;
    if (!(args >> token)) return;

    Position next;
    if (token == "startpos") {
        args >> token;  // possibly "moves"
    } else if (token == "fen") {
        while (args >> token && token != "moves") fen += token + " ";
        if (!next.set(fen)) {
            emit("info string illegal fen: " + fen);
            return;
        }
    } else {
        return;
    }

    if (token == "moves") {
        while (args >> token) {
            const Move m = parse_uci_move(next, token);
            if (m.is_none()) {
                emit("info string illegal move: " + token);
                return;  // keep the previous position rather than a half-applied one
            }
            next.do_move(m);
        }
    }
    pos_ = std::move(next);
}

void Uci::cmd_go(std::istream& args) {
    Limits limits;
    limits.multiPv = multiPv_;
    bool any = false;

    std::vector<std::string> tokens;
    for (std::string t; args >> t;) tokens.push_back(t);
    auto isKeyword = [](const std::string& t) {
        static const char* keys[] = {"searchmoves", "ponder", "wtime", "btime", "winc", "binc", "movestogo", "depth",
                                     "nodes", "mate", "movetime", "infinite", "perft"};
        for (const char* k : keys)
            if (t == k) return true;
        return false;
    };

    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const std::string& token = tokens[i];
        auto num = [&]() { return i + 1 < tokens.size() ? std::atoll(tokens[++i].c_str()) : 0LL; };
        if (token == "perft") {
            halt_search();
            std::ostringstream os;
            const int depth = int(num());
            const std::uint64_t nodes = perft_divide(pos_, depth > 0 ? depth : 1, &os);
            emit(os.str() + "\nNodes searched: " + std::to_string(nodes));
            return;
        }
        any = true;
        if (token == "searchmoves") {
            // The list runs until the next keyword. Moves that are not legal here are dropped by the search.
            while (i + 1 < tokens.size() && !isKeyword(tokens[i + 1])) {
                const Move m = parse_uci_move(pos_, tokens[++i]);
                if (!m.is_none()) limits.searchMoves.push_back(m);
            }
        } else if (token == "depth") limits.depth = int(num());
        else if (token == "nodes") limits.nodes = std::uint64_t(num());
        else if (token == "movetime") limits.movetime = num();
        else if (token == "wtime") limits.time[WHITE] = num();
        else if (token == "btime") limits.time[BLACK] = num();
        else if (token == "winc") limits.inc[WHITE] = num();
        else if (token == "binc") limits.inc[BLACK] = num();
        else if (token == "movestogo") limits.movestogo = int(num());
        else if (token == "infinite") limits.infinite = true;
        // Unknown tokens (ponder, mate, ...) are skipped.
    }
    if (!any) limits.infinite = true;  // plain "go" means search until "stop"

    search_.go(
        pos_, limits,
        [this](const SearchInfo& i) {
            std::ostringstream os;
            os << "info depth " << i.depth << " seldepth " << i.seldepth << " multipv " << i.multipv << " score "
               << format_score(i.score)
               << " nodes " << i.nodes << " nps " << (i.timeMs > 0 ? i.nodes * 1000 / std::uint64_t(i.timeMs) : i.nodes)
               << " hashfull " << i.hashfull << " time " << i.timeMs << " pv";
            for (Move m : i.pv) os << ' ' << to_uci(m);
            emit(os.str());
        },
        [this](const SearchResult& r) { emit("bestmove " + to_uci(r.best)); });
}

}  // namespace albatross
