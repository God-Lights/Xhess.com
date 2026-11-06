#include <atomic>
#include <sstream>
#include <string>

#include "core/movegen.h"
#include "search/search.h"
#include "search/see.h"
#include "search/tt.h"
#include "test.h"
#include "uci/uci.h"

using namespace albatross;

namespace {

int see_of(const char* fen, const char* uci) {
    Position p;
    CHECK(p.set(fen));
    const Move m = parse_uci_move(p, uci);
    CHECK(!m.is_none());
    return see(p, m);
}

}  // namespace

TEST(see_simple_exchanges) {
    // Pawn takes an undefended pawn.
    CHECK_EQ(see_of("4k3/8/8/3p4/4P3/8/8/4K3 w - - 0 1", "e4d5"), 100);
    // Pawn takes a pawn that is defended by a pawn: even trade.
    CHECK_EQ(see_of("4k3/8/2p5/3p4/4P3/8/8/4K3 w - - 0 1", "e4d5"), 0);
    // Queen takes a pawn defended by a pawn: loses the queen for a pawn.
    CHECK_EQ(see_of("4k3/8/2p5/3p4/8/8/3Q4/4K3 w - - 0 1", "d2d5"), 100 - 900);
    // Rook takes a knight defended by a pawn: 320 - 500.
    CHECK_EQ(see_of("4k3/8/2p5/3n4/8/8/3R4/4K3 w - - 0 1", "d2d5"), 320 - 500);
    // Capturing a free queen with a rook.
    CHECK_EQ(see_of("4k3/8/8/3q4/8/8/3R4/4K3 w - - 0 1", "d2d5"), 900);
}

TEST(see_handles_batteries_and_en_passant) {
    // Doubled rooks beat a single defender: Rxd5 Rxd5 Rxd5 wins a pawn (x-ray through the first rook).
    CHECK_EQ(see_of("3rk3/8/8/3p4/8/8/3R4/3RK3 w - - 0 1", "d2d5"), 100);
    // En passant takes a pawn that is not on the destination square.
    CHECK_EQ(see_of("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1", "e5d6"), 100);
    // A defended piece may not be taken by the king, but an undefended one may.
    CHECK_EQ(see_of("4k3/8/8/8/8/8/3r4/4K3 w - - 0 1", "e1d2"), 500);
}

TEST(see_of_a_quiet_move_is_zero) {
    Position p;
    CHECK_EQ(see(p, parse_uci_move(p, "e2e4")), 0);
}

TEST(null_move_flips_side_clears_ep_and_restores) {
    Position p;
    CHECK(p.set("rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 3"));
    const std::string before = p.fen();
    const Key keyBefore = p.key();

    p.do_null_move();
    CHECK(p.side_to_move() == BLACK);
    CHECK(p.ep_square() == SQ_NONE);
    CHECK(!p.in_check());
    // Same position as setting it up from FEN with the other side to move and no en passant square.
    Position q;
    CHECK(q.set("rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR b KQkq - 1 3"));
    CHECK_EQ(p.key(), q.key());
    CHECK(p.is_ok());

    // A real move can follow and be undone.
    const Move m = parse_uci_move(p, "g8f6");
    CHECK(!m.is_none());
    p.do_move(m);
    p.undo_move();

    p.undo_null_move();
    CHECK_EQ(p.fen(), before);
    CHECK_EQ(p.key(), keyBefore);
    CHECK(p.is_ok());
}

TEST(tt_store_and_probe) {
    TranspositionTable tt(1);
    const Key key = 0x123456789ABCDEF0ULL;
    CHECK(!tt.probe(key, 0).found);

    const Move m(SQ_E2, SQ_E4, DOUBLE_PUSH);
    tt.store(key, 7, 42, BOUND_EXACT, m, 0);
    const TTProbe r = tt.probe(key, 0);
    CHECK(r.found);
    CHECK_EQ(r.score, 42);
    CHECK_EQ(r.depth, 7);
    CHECK(r.bound == BOUND_EXACT);
    CHECK(r.move == m);
    CHECK(!tt.probe(key ^ (1ULL << 40), 0).found);  // different upper key bits: miss
}

TEST(tt_mate_scores_are_adjusted_by_ply) {
    TranspositionTable tt(1);
    const Key key = 0xDEADBEEFCAFEF00DULL;
    // At ply 5 the score "mate in 5 plies from the root" is MATE - 10 (the mate happens 5 plies below this node).
    tt.store(key, 4, MATE - 10, BOUND_EXACT, Move::none(), 5);
    // The same position found at ply 3 is two plies closer to the root: the mate is 2 plies further from it.
    CHECK_EQ(tt.probe(key, 3).score, MATE - 8);
    CHECK_EQ(tt.probe(key, 5).score, MATE - 10);
    tt.store(key, 4, -(MATE - 10), BOUND_EXACT, Move::none(), 5);
    CHECK_EQ(tt.probe(key, 3).score, -(MATE - 8));
}

TEST(tt_replacement_keeps_deeper_entries_in_the_same_search) {
    TranspositionTable tt(1);
    const Key key = 0x0F0F0F0F11223344ULL;
    tt.store(key, 10, 100, BOUND_LOWER, Move::none(), 0);
    tt.store(key, 2, -50, BOUND_UPPER, Move::none(), 0);  // much shallower: must not overwrite
    CHECK_EQ(tt.probe(key, 0).depth, 10);
    CHECK_EQ(tt.probe(key, 0).score, 100);
    tt.store(key, 12, 7, BOUND_EXACT, Move::none(), 0);   // deeper: replaces
    CHECK_EQ(tt.probe(key, 0).depth, 12);
    // After a new search starts, stale entries can be replaced by anything.
    tt.new_search();
    tt.store(key, 1, 3, BOUND_EXACT, Move::none(), 0);
    CHECK_EQ(tt.probe(key, 0).depth, 1);
    // A fail-low store without a move keeps the previous best move.
    const Move m(SQ_G1, SQ_F3, QUIET);
    tt.store(key, 3, 0, BOUND_EXACT, m, 0);
    tt.store(key, 4, -10, BOUND_UPPER, Move::none(), 0);
    CHECK(tt.probe(key, 0).move == m);
}

TEST(tt_clear_resize_and_hashfull) {
    TranspositionTable tt(1);
    CHECK_EQ(tt.hashfull(), 0);
    for (Key i = 0; i < 5000; ++i) tt.store(i * 0x9E3779B97F4A7C15ULL, 3, 0, BOUND_EXACT, Move::none(), 0);
    CHECK(tt.hashfull() > 0);
    tt.clear();
    CHECK_EQ(tt.hashfull(), 0);
    tt.resize(4);
    CHECK(tt.size() >= 4 * 1024 * 1024 / (2 * sizeof(TTEntry)));
}

TEST(search_with_a_shared_tt_gives_consistent_results) {
    // Searching the same position twice with one table (second time warm) must still find the mate.
    TranspositionTable tt(8);
    std::atomic<bool> stop{false};
    Limits l;
    l.depth = 5;
    Position p;
    CHECK(p.set("kbK5/pp6/1P6/8/8/8/8/R7 w - - 0 1"));
    for (int i = 0; i < 2; ++i) {
        const SearchResult r = search_position(p, l, stop, tt);
        CHECK_EQ(to_uci(r.best), std::string("a1a6"));
        CHECK_EQ(format_score(r.score), std::string("mate 2"));
    }
}

TEST(uci_options_hash_and_clear) {
    auto run = [](const char* script) {
        Uci uci;
        std::istringstream in(script);
        std::ostringstream out;
        uci.loop(in, out);
        return out.str();
    };
    const std::string hdr = run("uci\n");
    CHECK(hdr.find("option name Hash type spin default 16 min 1 max 4096") != std::string::npos);
    CHECK(hdr.find("option name Clear Hash type button") != std::string::npos);

    const std::string out = run("setoption name Hash value 32\nsetoption name Clear Hash\nposition startpos\ngo depth 4\n");
    CHECK(out.find("bestmove ") != std::string::npos);
    CHECK(out.find(" hashfull ") != std::string::npos);
    CHECK(run("setoption name Hash value 0\n").find("Hash must be between") != std::string::npos);
    CHECK(run("setoption name Hash value abc\n").find("invalid Hash value") != std::string::npos);
    CHECK(run("setoption name Nonexistent value 1\n").empty());  // unknown options are ignored silently
}
