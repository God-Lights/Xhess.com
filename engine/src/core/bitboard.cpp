#include "bitboard.h"

namespace albatross {

std::string pretty(Bitboard b) {
    std::string s;
    for (int r = 7; r >= 0; --r) {
        for (int f = 0; f < 8; ++f) s += (b & square_bb(make_square(f, r))) ? "X " : ". ";
        s += '\n';
    }
    return s;
}

}  // namespace albatross
