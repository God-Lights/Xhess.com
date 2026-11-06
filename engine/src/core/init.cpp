#include "init.h"

#include "attacks.h"
#include "zobrist.h"

namespace albatross {

void init() {
    static const bool done = [] {
        Attacks::init();
        Zobrist::init();
        return true;
    }();
    (void)done;
}

}  // namespace albatross
