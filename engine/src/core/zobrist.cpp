#include "zobrist.h"

namespace albatross::Zobrist {

Key psq[PIECE_NB][SQUARE_NB];
Key castling[CASTLING_RIGHT_NB];
Key enpassant[8];
Key side;

namespace {
// splitmix64: tiny, well-distributed, deterministic -> hashes are reproducible between runs and platforms.
struct SplitMix64 {
    std::uint64_t s;
    std::uint64_t next() {
        std::uint64_t z = (s += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
};
}  // namespace

void init() {
    SplitMix64 rng{0xA1BA7205ULL};
    for (int p = 0; p < PIECE_NB; ++p)
        for (int s = 0; s < SQUARE_NB; ++s) psq[p][s] = (type_of(Piece(p)) == NO_PIECE_TYPE || type_of(Piece(p)) > KING) ? 0 : rng.next();
    Key perRight[4];
    for (Key& k : perRight) k = rng.next();
    for (int m = 0; m < CASTLING_RIGHT_NB; ++m) {
        Key k = 0;
        for (int i = 0; i < 4; ++i)
            if (m & (1 << i)) k ^= perRight[i];
        castling[m] = k;
    }
    for (Key& k : enpassant) k = rng.next();
    side = rng.next();
}

}  // namespace albatross::Zobrist
