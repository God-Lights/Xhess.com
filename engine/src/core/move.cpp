#include "move.h"

namespace albatross {

std::string to_uci(Move m) {
    if (m.is_none()) return "0000";
    std::string s;
    s += char('a' + file_of(m.from()));
    s += char('1' + rank_of(m.from()));
    s += char('a' + file_of(m.to()));
    s += char('1' + rank_of(m.to()));
    if (m.is_promotion()) s += "??nbrq"[m.promotion_type()];  // index = PieceType (KNIGHT..QUEEN)
    return s;
}

}  // namespace albatross
