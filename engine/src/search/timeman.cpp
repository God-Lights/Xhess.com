#include "timeman.h"

#include <algorithm>

namespace albatross {

TimeBudget allocate_time(const Limits& l, Color us) {
    if (l.infinite) return {};
    if (l.movetime > 0) return {l.movetime, l.movetime};

    const std::int64_t t = l.time[us];
    if (t <= 0) return {};  // depth / nodes limited search

    constexpr std::int64_t Overhead = 20;  // ms reserved for communication and thread start
    const std::int64_t avail = std::max<std::int64_t>(1, t - Overhead);
    const int mtg = l.movestogo > 0 ? std::min(l.movestogo, 50) : 30;
    const std::int64_t base = t / mtg + l.inc[us] * 3 / 4;

    TimeBudget b;
    b.soft = std::max<std::int64_t>(1, std::min(base, avail / 2));
    b.hard = std::max(b.soft, std::min(base * 3, avail * 7 / 10));
    return b;
}

}  // namespace albatross
