#include <atomic>
#include <chrono>
#include <sstream>
#include <thread>

#include "core/movegen.h"
#include "search/search.h"
#include "search/timeman.h"
#include "test.h"
#include "uci/uci.h"

using namespace albatross;

namespace {

SearchResult run(const std::string& fen, int depth) {
    Position p;
    CHECK(p.set(fen));
    Limits l;
    l.depth = depth;
    std::atomic<bool> stop{false};
    return search_position(p, l, stop);
}

bool is_legal(const std::string& fen, Move m) {
    Position p;
    p.set(fen);
    return !parse_uci_move(p, to_uci(m)).is_none();
}

}  // namespace

TEST(search_finds_mate_in_one) {
    // Back-rank mate: 1.Ra8#
    const char* fen = "6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1";
    const SearchResult r = run(fen, 3);
    CHECK_EQ(to_uci(r.best), std::string("a1a8"));
    CHECK_EQ(format_score(r.score), std::string("mate 1"));
    // Scholar's mate: 1.Qxf7#
    const SearchResult s = run("r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 4 4", 3);
    CHECK_EQ(to_uci(s.best), std::string("h5f7"));
    CHECK_EQ(format_score(s.score), std::string("mate 1"));
}

TEST(search_finds_mate_in_two) {
    // 1.Ra6! bxa6 2.b7#
    const SearchResult r = run("kbK5/pp6/1P6/8/8/8/8/R7 w - - 0 1", 5);
    CHECK_EQ(to_uci(r.best), std::string("a1a6"));
    CHECK_EQ(format_score(r.score), std::string("mate 2"));
}

TEST(search_defends_against_a_mate_threat) {
    // Black to move; Ra8# is threatened. Making luft (...g6/h6/g5/h5/...Kf8 etc.) avoids mate, so the score is not a mate score.
    const char* fen = "6k1/5ppp/8/8/8/8/5PPP/R5K1 b - - 0 1";
    const SearchResult r = run(fen, 4);
    CHECK(is_legal(fen, r.best));
    CHECK(!is_mate_score(r.score));
    // The only defences are king or pawn moves that give the king a flight square.
    Position p;
    p.set(fen);
    p.do_move(parse_uci_move(p, to_uci(r.best)));
    Limits l;
    l.depth = 2;
    std::atomic<bool> stop{false};
    CHECK(search_position(p, l, stop).score < MATE_IN_MAX_PLY);  // White has no immediate forced mate afterwards
}

TEST(search_captures_hanging_material_and_avoids_blunders) {
    // The black queen is attacked by a pawn: exd5.
    CHECK_EQ(to_uci(run("4k3/8/8/3q4/4P3/8/8/4K3 w - - 0 1", 2).best), std::string("e4d5"));
    // Taking the defended pawn with the queen loses the queen: Qxb7?? Rxb7. Depth 3 must avoid it.
    const SearchResult r = run("1r2k3/1p6/8/8/8/8/8/1Q2K3 w - - 0 1", 3);
    CHECK(to_uci(r.best) != "b1b7");
}

TEST(search_does_not_stalemate_when_winning) {
    // White has K+Q vs K and must not play ...Qg6?? / Qc7?? style stalemates; the result must stay winning.
    const SearchResult r = run("7k/8/5K2/8/8/8/8/6Q1 w - - 0 1", 6);
    CHECK(r.score > 500);
    Position p;
    p.set("7k/8/5K2/8/8/8/8/6Q1 w - - 0 1");
    p.do_move(parse_uci_move(p, to_uci(r.best)));
    MoveList l;
    generate_legal(p, l);
    CHECK(!l.empty() || p.in_check());  // either Black has a move or it is mate, never stalemate
}

TEST(search_scores_dead_draw_as_zero) {
    CHECK_EQ(run("8/8/8/4k3/8/8/1K6/8 w - - 0 1", 4).score, 0);   // K vs K
    CHECK_EQ(run("4k3/8/8/8/8/8/8/4KN2 w - - 0 1", 4).score, 0);  // K+N vs K
}

TEST(search_has_no_move_in_terminal_positions) {
    CHECK(run("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1", 3).best.is_none());  // stalemate
    const SearchResult mated = run("rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3", 3);
    CHECK(mated.best.is_none());
    CHECK_EQ(format_score(mated.score), std::string("mate 0"));
}

TEST(uci_reports_mate_and_stalemate_without_moves) {
    auto reply = [](const char* script) {
        Uci uci;
        std::istringstream in(script);
        std::ostringstream out;
        uci.loop(in, out);
        return out.str();
    };
    const std::string mate = reply("position startpos moves f2f3 e7e5 g2g4 d8h4\ngo depth 3\n");
    CHECK(mate.find("info depth 0 seldepth 0 score mate 0 ") != std::string::npos);
    CHECK(mate.find("bestmove 0000") != std::string::npos);
    const std::string stale = reply("position fen 7k/5Q2/6K1/8/8/8/8/8 b - - 0 1\ngo depth 3\n");
    CHECK(stale.find("info depth 0 seldepth 0 score cp 0 ") != std::string::npos);
    CHECK(stale.find("bestmove 0000") != std::string::npos);
}

TEST(search_respects_depth_and_node_limits) {
    Position p;
    std::atomic<bool> stop{false};
    Limits l;
    l.depth = 4;
    int lastDepth = 0;
    const SearchResult r = search_position(p, l, stop, [&](const SearchInfo& i) {
        CHECK(i.depth == lastDepth + 1);  // iterative deepening reports every depth in order
        CHECK(!i.pv.empty());
        lastDepth = i.depth;
    });
    CHECK_EQ(r.depth, 4);
    CHECK_EQ(lastDepth, 4);

    Limits n;
    n.nodes = 20000;
    const SearchResult rn = search_position(p, n, stop);
    CHECK(rn.depth >= 1);
    CHECK(rn.nodes < 20000 + 4096);  // limit is polled every 2048 nodes
    CHECK(is_legal(Position::START_FEN, rn.best));
}

TEST(search_pv_is_a_legal_line) {
    Position p;
    CHECK(p.set("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"));
    Limits l;
    l.depth = 5;
    std::atomic<bool> stop{false};
    const SearchResult r = search_position(p, l, stop);
    CHECK(!r.pv.empty());
    CHECK(r.pv[0] == r.best);
    int plies = 0;
    for (Move m : r.pv) {
        const Move legal = parse_uci_move(p, to_uci(m));
        CHECK(!legal.is_none());
        if (legal.is_none()) break;
        p.do_move(legal);
        ++plies;
    }
    CHECK_EQ(plies, int(r.pv.size()));
}

TEST(search_leaves_the_position_untouched) {
    Position p;
    p.set("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    const std::string fen = p.fen();
    const Key key = p.key();
    Limits l;
    l.depth = 4;
    std::atomic<bool> stop{false};
    search_position(p, l, stop);
    CHECK_EQ(p.fen(), fen);
    CHECK_EQ(p.key(), key);
}

TEST(search_avoids_repeating_when_better) {
    // After 1.Nf3 Nf6 2.Ng1 Ng8 3.Nf3 Nf6 4.Ng1 the move ...Ng8 would repeat the position a third time.
    // The search treats a repeated position as a draw, so it must still return a legal move.
    Position p;
    for (const char* m : {"g1f3", "g8f6", "f3g1", "f6g8", "g1f3", "g8f6", "f3g1"}) p.do_move(parse_uci_move(p, m));
    Limits l;
    l.depth = 3;
    std::atomic<bool> stop{false};
    const SearchResult r = search_position(p, l, stop);
    CHECK(!r.best.is_none());
}

TEST(search_stops_on_request) {
    Search s;
    Position p;
    Limits l;
    l.infinite = true;
    std::atomic<bool> got{false};
    Move best;
    s.go(p, l, {}, [&](const SearchResult& r) {
        best = r.best;
        got = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(!got.load());  // infinite search keeps running
    const auto t0 = std::chrono::steady_clock::now();
    s.stop();
    s.wait();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    CHECK(got.load());
    CHECK(ms < 1000);
    CHECK(is_legal(Position::START_FEN, best));
}

TEST(time_allocation) {
    Limits fixed;
    fixed.movetime = 500;
    CHECK_EQ(allocate_time(fixed, WHITE).hard, 500);
    CHECK_EQ(allocate_time(fixed, WHITE).soft, 500);

    Limits clock;
    clock.time[WHITE] = 60000;
    clock.inc[WHITE] = 1000;
    const TimeBudget b = allocate_time(clock, WHITE);
    CHECK(b.soft > 1000 && b.soft < 5000);
    CHECK(b.hard >= b.soft && b.hard < 30000);

    Limits low;
    low.time[BLACK] = 30;
    const TimeBudget lb = allocate_time(low, BLACK);
    CHECK(lb.soft >= 1 && lb.hard >= lb.soft && lb.hard < 30);

    Limits depthOnly;
    depthOnly.depth = 6;
    CHECK_EQ(allocate_time(depthOnly, WHITE).hard, 0);
    Limits inf;
    inf.infinite = true;
    inf.time[WHITE] = 1000;
    CHECK_EQ(allocate_time(inf, WHITE).hard, 0);
}

TEST(search_movetime_is_honored) {
    Position p;
    Limits l;
    l.movetime = 200;
    std::atomic<bool> stop{false};
    const auto t0 = std::chrono::steady_clock::now();
    const SearchResult r = search_position(p, l, stop);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    CHECK(ms < 600);
    CHECK(r.depth >= 1);
    CHECK(is_legal(Position::START_FEN, r.best));
}

TEST(uci_go_prints_info_and_bestmove) {
    Uci uci;
    std::istringstream in("position startpos\ngo depth 4\n");
    std::ostringstream out;
    uci.loop(in, out);  // end of input lets a finite search finish
    const std::string s = out.str();
    CHECK(s.find("info depth 1 ") != std::string::npos);
    CHECK(s.find("info depth 4 ") != std::string::npos);
    CHECK(s.find(" score cp ") != std::string::npos);
    CHECK(s.find(" pv ") != std::string::npos);
    CHECK(s.find("bestmove ") != std::string::npos);
}

TEST(uci_go_infinite_is_stopped_at_end_of_input) {
    Uci uci;
    std::istringstream in("position startpos\ngo infinite\n");
    std::ostringstream out;
    uci.loop(in, out);
    CHECK(out.str().find("bestmove ") != std::string::npos);
}

TEST(uci_score_formatting) {
    CHECK_EQ(format_score(37), std::string("cp 37"));
    CHECK_EQ(format_score(-120), std::string("cp -120"));
    CHECK_EQ(format_score(MATE - 1), std::string("mate 1"));
    CHECK_EQ(format_score(MATE - 3), std::string("mate 2"));
    CHECK_EQ(format_score(-(MATE - 4)), std::string("mate -2"));
    CHECK_EQ(format_score(-MATE), std::string("mate 0"));
}
