#include "perft.h"

#include <ostream>

#include "movegen.h"

namespace albatross {

std::uint64_t perft(Position& pos, int depth) {
    if (depth <= 0) return 1;
    MoveList list;
    generate_legal(pos, list);
    if (depth == 1) return list.size();
    std::uint64_t nodes = 0;
    for (Move m : list) {
        pos.do_move(m);
        nodes += perft(pos, depth - 1);
        pos.undo_move();
    }
    return nodes;
}

std::uint64_t perft_divide(Position& pos, int depth, std::ostream* out) {
    if (depth <= 0) return 1;
    MoveList list;
    generate_legal(pos, list);
    std::uint64_t total = 0;
    for (Move m : list) {
        pos.do_move(m);
        const std::uint64_t n = depth == 1 ? 1 : perft(pos, depth - 1);
        pos.undo_move();
        if (out) *out << to_uci(m) << ": " << n << '\n';
        total += n;
    }
    return total;
}

}  // namespace albatross
