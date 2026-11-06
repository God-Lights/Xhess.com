#include "uci.h"

#include <iostream>
#include <sstream>

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
        emit(std::string("id name ") + NAME + "\nid author " + AUTHOR + "\nuciok");
    } else if (cmd == "isready") {
        emit("readyok");
    } else if (cmd == "ucinewgame") {
        halt_search();
        pos_.set(Position::START_FEN);
    } else if (cmd == "position") {
        halt_search();
        cmd_position(is);
    } else if (cmd == "go") {
        cmd_go(is);
    } else if (cmd == "stop") {
        search_.stop();
    } else if (cmd == "setoption") {
        // No options yet (Hash/Threads/MultiPV arrive later). Accepted and ignored.
    } else if (cmd == "d") {
        emit(pos_.pretty());
    } else if (cmd == "quit") {
        halt_search();
        return false;
    } else {
        emit("info string unknown command: " + cmd);
    }
    return true;
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
    bool any = false;
    std::string token;
    while (args >> token) {
        auto num = [&args]() {
            long long v = 0;
            args >> v;
            return v;
        };
        if (token == "perft") {
            halt_search();
            std::ostringstream os;
            const int depth = int(num());
            const std::uint64_t nodes = perft_divide(pos_, depth > 0 ? depth : 1, &os);
            emit(os.str() + "\nNodes searched: " + std::to_string(nodes));
            return;
        }
        any = true;
        if (token == "depth") limits.depth = int(num());
        else if (token == "nodes") limits.nodes = std::uint64_t(num());
        else if (token == "movetime") limits.movetime = num();
        else if (token == "wtime") limits.time[WHITE] = num();
        else if (token == "btime") limits.time[BLACK] = num();
        else if (token == "winc") limits.inc[WHITE] = num();
        else if (token == "binc") limits.inc[BLACK] = num();
        else if (token == "movestogo") limits.movestogo = int(num());
        else if (token == "infinite") limits.infinite = true;
        // Unknown tokens (ponder, searchmoves, mate, ...) are skipped.
    }
    if (!any) limits.infinite = true;  // plain "go" means search until "stop"

    search_.go(
        pos_, limits,
        [this](const SearchInfo& i) {
            std::ostringstream os;
            os << "info depth " << i.depth << " seldepth " << i.seldepth << " score " << format_score(i.score)
               << " nodes " << i.nodes << " nps " << (i.timeMs > 0 ? i.nodes * 1000 / std::uint64_t(i.timeMs) : i.nodes)
               << " time " << i.timeMs << " pv";
            for (Move m : i.pv) os << ' ' << to_uci(m);
            emit(os.str());
        },
        [this](const SearchResult& r) { emit("bestmove " + to_uci(r.best)); });
}

}  // namespace albatross
