#include "uci.h"

#include <iostream>
#include <sstream>

#include "../core/movegen.h"
#include "../core/perft.h"

namespace albatross {

Uci::Uci() = default;

void Uci::loop(std::istream& in, std::ostream& out) {
    std::string line;
    while (std::getline(in, line)) {
        if (!execute(line, out)) break;
        out.flush();
    }
}

bool Uci::execute(const std::string& line, std::ostream& out) {
    std::istringstream is(line);
    std::string cmd;
    if (!(is >> cmd)) return true;

    if (cmd == "uci") {
        out << "id name " << NAME << "\nid author " << AUTHOR << "\nuciok\n";
    } else if (cmd == "isready") {
        out << "readyok\n";
    } else if (cmd == "ucinewgame") {
        pos_.set(Position::START_FEN);
    } else if (cmd == "position") {
        cmd_position(is, out);
    } else if (cmd == "go") {
        cmd_go(is, out);
    } else if (cmd == "setoption") {
        // No options yet (Hash/Threads/MultiPV arrive with the search). Accepted and ignored.
    } else if (cmd == "stop") {
        // Nothing runs in the background yet.
    } else if (cmd == "d") {
        out << pos_.pretty();
    } else if (cmd == "quit") {
        return false;
    } else {
        out << "info string unknown command: " << cmd << '\n';
    }
    return true;
}

void Uci::cmd_position(std::istream& args, std::ostream& out) {
    std::string token, fen;
    if (!(args >> token)) return;

    Position next;
    if (token == "startpos") {
        args >> token;  // possibly "moves"
    } else if (token == "fen") {
        while (args >> token && token != "moves") fen += token + " ";
        if (!next.set(fen)) {
            out << "info string illegal fen: " << fen << '\n';
            return;
        }
    } else {
        return;
    }

    if (token == "moves") {
        while (args >> token) {
            const Move m = parse_uci_move(next, token);
            if (m.is_none()) {
                out << "info string illegal move: " << token << '\n';
                return;  // keep the previous position rather than a half-applied one
            }
            next.do_move(m);
        }
    }
    pos_ = std::move(next);
}

void Uci::cmd_go(std::istream& args, std::ostream& out) {
    std::string token;
    while (args >> token) {
        if (token == "perft") {
            int depth = 1;
            args >> depth;
            const std::uint64_t nodes = perft_divide(pos_, depth, &out);
            out << "\nNodes searched: " << nodes << '\n';
            return;
        }
    }
    // Albatross 0.1 has no search: answer with the first legal move so a GUI never hangs.
    MoveList list;
    generate_legal(pos_, list);
    out << "bestmove " << (list.empty() ? "0000" : to_uci(list[0])) << '\n';
}

}  // namespace albatross
