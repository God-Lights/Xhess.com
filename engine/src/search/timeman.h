#pragma once

#include <cstdint>

#include "../core/types.h"
#include "limits.h"

namespace albatross {

struct TimeBudget {
    std::int64_t soft = 0;  // ms. Do not start a new iteration once about half of it has been used. 0 = unlimited
    std::int64_t hard = 0;  // ms. Abort the running iteration. 0 = unlimited
};

// Decides how long to think. movetime is used as given; with a clock a fraction of the remaining
// time plus most of the increment is budgeted; depth/nodes/infinite searches have no time budget.
TimeBudget allocate_time(const Limits& limits, Color us);

}  // namespace albatross
