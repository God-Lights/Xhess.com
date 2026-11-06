#pragma once

namespace albatross {

// Builds the attack tables and Zobrist keys. Idempotent and thread-safe; Position calls it itself.
void init();

}  // namespace albatross
