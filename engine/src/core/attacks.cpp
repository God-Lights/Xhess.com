#include "attacks.h"

#include <vector>

namespace albatross::Attacks {

Bitboard PawnAttacks[COLOR_NB][SQUARE_NB];
Bitboard KnightAttacks[SQUARE_NB];
Bitboard KingAttacks[SQUARE_NB];
Magic RookMagics[SQUARE_NB];
Magic BishopMagics[SQUARE_NB];

namespace {

Bitboard RookTable[0x19000];   // sum over squares of 2^(relevant occupancy bits)
Bitboard BishopTable[0x1480];

struct Step { int df, dr; };
constexpr Step RookSteps[4] = {{0, 1}, {0, -1}, {1, 0}, {-1, 0}};
constexpr Step BishopSteps[4] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};

bool on_board(int f, int r) { return f >= 0 && f < 8 && r >= 0 && r < 8; }

// Reference implementation: walk the rays until a blocker (included) or the edge.
Bitboard slow_attacks(Square s, Bitboard occupied, const Step (&steps)[4]) {
    Bitboard result = 0;
    for (const Step& d : steps) {
        int f = file_of(s) + d.df, r = rank_of(s) + d.dr;
        while (on_board(f, r)) {
            Bitboard b = square_bb(make_square(f, r));
            result |= b;
            if (occupied & b) break;
            f += d.df;
            r += d.dr;
        }
    }
    return result;
}

// Relevant occupancy: rays without the last (edge) square, since the edge never changes the attacks.
Bitboard relevant_mask(Square s, const Step (&steps)[4]) {
    Bitboard mask = 0;
    for (const Step& d : steps) {
        int f = file_of(s) + d.df, r = rank_of(s) + d.dr;
        while (on_board(f + d.df, r + d.dr)) {
            mask |= square_bb(make_square(f, r));
            f += d.df;
            r += d.dr;
        }
    }
    return mask;
}

struct Prng {
    std::uint64_t s;
    std::uint64_t next() {
        s ^= s >> 12;
        s ^= s << 25;
        s ^= s >> 27;
        return s * 2685821657736338717ULL;
    }
    std::uint64_t sparse() { return next() & next() & next(); }
};

void init_magics(Magic (&magics)[SQUARE_NB], Bitboard* table, const Step (&steps)[4], std::uint64_t seed) {
    Prng rng{seed};
    std::vector<Bitboard> occupancy(4096), reference(4096);
    std::vector<int> epoch(4096, 0);
    int counter = 0;
    std::size_t offset = 0;

    for (int i = 0; i < SQUARE_NB; ++i) {
        const Square s = Square(i);
        const Bitboard mask = relevant_mask(s, steps);
        const int bits = popcount(mask);
        const unsigned shift = 64 - bits;
        const std::size_t size = std::size_t(1) << bits;

        // Enumerate all subsets of the mask (Carry-Rippler trick).
        int n = 0;
        Bitboard b = 0;
        do {
            occupancy[n] = b;
            reference[n] = slow_attacks(s, b, steps);
            ++n;
            b = (b - mask) & mask;
        } while (b);

        Magic& m = magics[i];
        m.mask = mask;
        m.shift = shift;
        m.attacks = table + offset;

        for (;;) {
            const Bitboard magic = rng.sparse();
            if (popcount((mask * magic) >> 56) < 6) continue;
            ++counter;
            bool ok = true;
            for (int k = 0; k < n; ++k) {
                const unsigned idx = unsigned((occupancy[k] * magic) >> shift);
                if (epoch[idx] != counter) {
                    epoch[idx] = counter;
                    m.attacks[idx] = reference[k];
                } else if (m.attacks[idx] != reference[k]) {
                    ok = false;
                    break;
                }
            }
            if (ok) {
                m.magic = magic;
                break;
            }
        }
        offset += size;
    }
}

}  // namespace

void init() {
    for (int i = 0; i < SQUARE_NB; ++i) {
        const Bitboard b = square_bb(Square(i));
        PawnAttacks[WHITE][i] = shift<NORTH_EAST>(b) | shift<NORTH_WEST>(b);
        PawnAttacks[BLACK][i] = shift<SOUTH_EAST>(b) | shift<SOUTH_WEST>(b);
        KingAttacks[i] = shift<NORTH>(b) | shift<SOUTH>(b) | shift<EAST>(b) | shift<WEST>(b) |
                         shift<NORTH_EAST>(b) | shift<NORTH_WEST>(b) | shift<SOUTH_EAST>(b) | shift<SOUTH_WEST>(b);
        Bitboard n = 0;
        n |= shift<NORTH>(shift<NORTH_EAST>(b)) | shift<NORTH>(shift<NORTH_WEST>(b));
        n |= shift<SOUTH>(shift<SOUTH_EAST>(b)) | shift<SOUTH>(shift<SOUTH_WEST>(b));
        n |= shift<EAST>(shift<NORTH_EAST>(b)) | shift<EAST>(shift<SOUTH_EAST>(b));
        n |= shift<WEST>(shift<NORTH_WEST>(b)) | shift<WEST>(shift<SOUTH_WEST>(b));
        KnightAttacks[i] = n;
    }
    init_magics(RookMagics, RookTable, RookSteps, 0x9E3779B97F4A7C15ULL);
    init_magics(BishopMagics, BishopTable, BishopSteps, 0xD1B54A32D192ED03ULL);
}

}  // namespace albatross::Attacks
