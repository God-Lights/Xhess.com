#include <string>
#include <vector>

#include "core/movegen.h"
#include "core/position.h"
#include "test.h"

using namespace albatross;

namespace {

const std::vector<std::string> Fens = {
    Position::START_FEN,
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
    "rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 3",
};

// Full snapshot of the observable state.
struct Snapshot {
    std::string fen;
    Key key;
    Bitboard w, b;
    bool operator==(const Snapshot&) const = default;
};
Snapshot snap(const Position& p) { return {p.fen(), p.key(), p.pieces(WHITE), p.pieces(BLACK)}; }

// Walk the tree: after every do_move the incremental key must match a fresh computation and is_ok() must hold;
// after every undo_move the position must be bit-for-bit what it was before.
void walk(Position& p, int depth, int& visited) {
    if (depth == 0) return;
    MoveList list;
    generate_legal(p, list);
    for (Move m : list) {
        const Snapshot before = snap(p);
        p.do_move(m);
        ++visited;
        CHECK_EQ(p.key(), p.compute_key());
        CHECK(p.is_ok());
        walk(p, depth - 1, visited);
        p.undo_move();
        CHECK(snap(p) == before);
    }
}

}  // namespace

TEST(fen_round_trip) {
    for (const auto& f : Fens) {
        Position p;
        CHECK(p.set(f));
        CHECK_EQ(p.fen(), f);
        CHECK(p.is_ok());
    }
}

TEST(fen_rejects_illegal_input) {
    Position p;
    const std::string before = p.fen();
    const char* bad[] = {
        "",
        "8/8/8/8/8/8/8/8 w - - 0 1",                              // no kings
        "4k3/8/8/8/8/8/8/3KK3 w - - 0 1",                         // two white kings
        "4k3/8/8/8/8/8/8/P3K3 w - - 0 1",                         // pawn on rank 1
        "4k3/8/8/8/8/8/8/4K3 x - - 0 1",                          // bad side
        "4k3/8/8/8/8/8/8/4K3 w X - 0 1",                          // bad castling char
        "4k3/8/8/8/8/8/8/4K3 w - e5 0 1",                         // ep on wrong rank
        "4k3/8/8/8/8/8/8/4K3 w - e6 0 1",                         // ep without a pawn behind it
        "4k3/8/8/8/8/8/8/4K2 w - - 0 1",                          // short rank
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR/8 w - - 0 1",  // 9 ranks
        "4k3/8/8/8/8/8/4R3/4K3 w - - 0 1",                        // black king attacked, white to move
    };
    for (const char* f : bad) {
        CHECK(!p.set(f));
        CHECK_EQ(p.fen(), before);  // failed set() must not modify the position
    }
}

TEST(castling_rights_are_sanitized) {
    Position p;
    CHECK(p.set("4k3/8/8/8/8/8/8/R3K3 w KQkq - 0 1"));  // only the white queenside rook exists
    CHECK_EQ(p.castling_rights(), int(WHITE_OOO));
}

TEST(make_unmake_restores_state_and_hash_is_consistent) {
    int visited = 0;
    for (const auto& f : Fens) {
        Position p;
        CHECK(p.set(f));
        walk(p, 3, visited);
    }
    CHECK(visited > 100000);
}

TEST(transposition_gives_same_hash) {
    Position a, b;
    for (const char* m : {"g1f3", "g8f6", "b1c3", "b8c6"}) a.do_move(parse_uci_move(a, m));
    for (const char* m : {"b1c3", "b8c6", "g1f3", "g8f6"}) b.do_move(parse_uci_move(b, m));
    CHECK_EQ(a.key(), b.key());
    Position c;
    CHECK(c.key() != a.key());
}

TEST(ep_square_only_hashed_when_capturable) {
    // After 1.e4 no black pawn can capture on e3, so the position hashes like the same board without ep.
    Position a, b;
    a.do_move(parse_uci_move(a, "e2e4"));
    CHECK(b.set("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1"));
    CHECK_EQ(a.key(), b.key());
    // With a black pawn on d4 the ep capture exists, so the keys must differ.
    Position c, d;
    CHECK(c.set("4k3/8/8/8/3p4/8/4P3/4K3 w - - 0 1"));
    c.do_move(parse_uci_move(c, "e2e4"));
    CHECK(d.set("4k3/8/8/8/3pP3/8/8/4K3 b - - 0 1"));
    CHECK(c.key() != d.key());
    CHECK_EQ(c.key(), c.compute_key());
}

TEST(repetition_and_fifty_move) {
    Position p;
    CHECK_EQ(p.repetition_count(), 1);
    const char* cycle[] = {"g1f3", "g8f6", "f3g1", "f6g8"};
    for (int round = 0; round < 2; ++round)
        for (const char* m : cycle) p.do_move(parse_uci_move(p, m));
    CHECK_EQ(p.repetition_count(), 3);
    for (int round = 0; round < 2; ++round)
        for (const char* m : cycle) p.do_move(parse_uci_move(p, m));
    CHECK_EQ(p.repetition_count(), 5);
    CHECK(p.is_fivefold_repetition());
    p.do_move(parse_uci_move(p, "e2e4"));  // pawn move resets the counter
    CHECK_EQ(p.repetition_count(), 1);
    CHECK_EQ(p.rule50(), 0);

    Position q;
    CHECK(q.set("4k3/8/8/8/8/8/8/4K2R w - - 99 80"));
    CHECK(!q.is_fifty_move_draw());
    q.do_move(parse_uci_move(q, "h1h2"));
    CHECK(q.is_fifty_move_draw());
    CHECK(!q.is_seventyfive_move_draw());
    CHECK(q.set("4k3/8/8/8/8/8/8/4K2R w - - 150 120"));
    CHECK(q.is_seventyfive_move_draw());
}

TEST(insufficient_material) {
    struct { const char* fen; bool dead; } cases[] = {
        {"8/8/8/8/8/8/8/K6k w - - 0 1", true},        // K v K
        {"8/8/8/8/8/8/8/KB5k w - - 0 1", true},       // K+B v K
        {"8/8/8/8/8/8/8/KN5k w - - 0 1", true},       // K+N v K
        {"8/8/8/8/8/2b5/8/K1B4k w - - 0 1", true},    // bishops on the same color
        {"8/8/8/8/8/3b4/8/K1B4k w - - 0 1", false},   // bishops on opposite colors
        {"8/8/8/8/8/8/8/KNN4k w - - 0 1", false},     // KNN v K can be mated with help
        {"8/8/8/8/8/8/P7/K6k w - - 0 1", false},      // pawn
        {"8/8/8/8/8/8/R7/K6k w - - 0 1", false},       // rook
    };
    for (const auto& c : cases) {
        Position p;
        CHECK(p.set(c.fen));
        CHECK_EQ(p.insufficient_material(), c.dead);
    }
}
