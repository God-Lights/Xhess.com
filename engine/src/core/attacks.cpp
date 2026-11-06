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

// Magic numbers found once by the search below (fixed seeds, see init_magics) and stored here so that
// start-up does not repeat the search (~330 ms). init_magics() verifies every entry while filling the tables
// and falls back to the search if one of them does not work.
constexpr Bitboard RookMagicNumbers[SQUARE_NB] = {
    0x1080004008801020ULL, 0x0840092002c03000ULL,
    0x1900200010400900ULL, 0x0880100008000480ULL,
    0x4200100420080200ULL, 0x8100020100080400ULL,
    0x0200040110886200ULL, 0x0200008040220411ULL,
    0x0404800084400220ULL, 0x0000401000402000ULL,
    0x0086001081220440ULL, 0x0408800800100280ULL,
    0x000a001201040820ULL, 0x8848800200840080ULL,
    0x4001000100040200ULL, 0x0442000102105084ULL,
    0x9080010020804100ULL, 0x0040404000201009ULL,
    0x0000808010002009ULL, 0x2200090021d00100ULL,
    0x0008008008040080ULL, 0x0004004002010040ULL,
    0x0011040008015042ULL, 0x00000a0001768104ULL,
    0x0000800080204009ULL, 0x2010004140002001ULL,
    0x9800200280100080ULL, 0x1000100080080080ULL,
    0x0442000a00049020ULL, 0x2100040080020080ULL,
    0x0800120400900148ULL, 0x0010040a00128541ULL,
    0x2800804000800030ULL, 0x1010002000400041ULL,
    0x4000200011004100ULL, 0x0610008410800800ULL,
    0x0400802402800800ULL, 0xc100020080800400ULL,
    0x0002000802000401ULL, 0x0182085882000401ULL,
    0x0220204000808000ULL, 0x2860100040024022ULL,
    0x0001002004110040ULL, 0x99101042000a0020ULL,
    0x0004080004008080ULL, 0x0010040002008080ULL,
    0x2012004881020004ULL, 0x8300842444820011ULL,
    0x0088403882010200ULL, 0x0820400080210100ULL,
    0x0110910040a00300ULL, 0x0801100280080480ULL,
    0x0242009008200600ULL, 0x1002000489500200ULL,
    0x0040800200010080ULL, 0x0091800041000080ULL,
    0x0000209300488001ULL, 0x04c1002414824001ULL,
    0x020020000b001041ULL, 0x7000100004200901ULL,
    0x8002002004100802ULL, 0x30010002084c0007ULL,
    0x0888221800813004ULL, 0x4000002840840112ULL,
};

constexpr Bitboard BishopMagicNumbers[SQUARE_NB] = {
    0x2048017020910100ULL, 0x0044410424008008ULL,
    0x040828a400900000ULL, 0x8002209200022000ULL,
    0x0002021000540002ULL, 0x0021018840000000ULL,
    0x00009e8420204002ULL, 0x00a0920110084480ULL,
    0x4003062018010110ULL, 0x0221046812004e09ULL,
    0x01e11002958912a0ULL, 0x0000044410804000ULL,
    0x0000821210000080ULL, 0x080201102210a800ULL,
    0x0080040411045004ULL, 0x00704a1842021000ULL,
    0x1005061070322800ULL, 0x0018001010410444ULL,
    0x0010000800401420ULL, 0x2204002844000800ULL,
    0x2052020412022280ULL, 0x000a020101008208ULL,
    0x0040400201042000ULL, 0x03e1082040480410ULL,
    0x1004200004208414ULL, 0x08700400984808c8ULL,
    0x0088080004004410ULL, 0x008c0240140100a2ULL,
    0x0008840001822000ULL, 0x0050088001080100ULL,
    0x98140840040a2200ULL, 0x3002020900210110ULL,
    0x1004040640206000ULL, 0x1090909000840400ULL,
    0x9002444810100020ULL, 0x4000020080080080ULL,
    0x0028020400011010ULL, 0x0290808300020100ULL,
    0x8010020882004410ULL, 0x0604010040082c20ULL,
    0x20040104c0801008ULL, 0x6004208424001050ULL,
    0x1002840041000800ULL, 0x0200042018000102ULL,
    0xa8002000a0821c00ULL, 0x0040080802201910ULL,
    0x0222620444000100ULL, 0x0002080041020088ULL,
    0x1500820110401050ULL, 0x0000492090100080ULL,
    0x0900410041100000ULL, 0x0302000420880000ULL,
    0x0010501202020020ULL, 0x0008200490049040ULL,
    0x0462080214a40120ULL, 0x2421310102008100ULL,
    0x2400420080884060ULL, 0x0800804406184208ULL,
    0x0b0080124a084400ULL, 0x082e082300840412ULL,
    0x6051049040082200ULL, 0xc610211002102101ULL,
    0x0000048808010433ULL, 0x0010200804405440ULL,
};

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

// Fills `table` for one square with the given magic. Returns false if two occupancies that need different
// attack sets collide (the magic is not usable).
bool fill_table(Magic& m, const Bitboard* occupancy, const Bitboard* reference, int n, Bitboard magic,
                std::vector<int>& epoch, int& counter) {
    ++counter;
    for (int k = 0; k < n; ++k) {
        const unsigned idx = unsigned((occupancy[k] * magic) >> m.shift);
        if (epoch[idx] != counter) {
            epoch[idx] = counter;
            m.attacks[idx] = reference[k];
        } else if (m.attacks[idx] != reference[k]) {
            return false;
        }
    }
    return true;
}

void init_magics(Magic (&magics)[SQUARE_NB], Bitboard* table, const Step (&steps)[4], std::uint64_t seed,
                 const Bitboard (&known)[SQUARE_NB]) {
    Prng rng{seed};
    std::vector<Bitboard> occupancy(4096), reference(4096);
    std::vector<int> epoch(4096, 0);
    int counter = 0;
    std::size_t offset = 0;

    for (int i = 0; i < SQUARE_NB; ++i) {
        const Square s = Square(i);
        const Bitboard mask = relevant_mask(s, steps);
        const int bits = popcount(mask);
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
        m.shift = 64 - bits;
        m.attacks = table + offset;

        if (fill_table(m, occupancy.data(), reference.data(), n, known[i], epoch, counter)) {
            m.magic = known[i];
        } else {
            // Fallback: search for a magic (sparse random numbers).
            for (;;) {
                const Bitboard magic = rng.sparse();
                if (popcount((mask * magic) >> 56) < 6) continue;
                if (fill_table(m, occupancy.data(), reference.data(), n, magic, epoch, counter)) {
                    m.magic = magic;
                    break;
                }
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
    init_magics(RookMagics, RookTable, RookSteps, 0x9E3779B97F4A7C15ULL, RookMagicNumbers);
    init_magics(BishopMagics, BishopTable, BishopSteps, 0xD1B54A32D192ED03ULL, BishopMagicNumbers);
}

}  // namespace albatross::Attacks
