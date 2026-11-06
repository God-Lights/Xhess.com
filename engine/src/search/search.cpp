#include "search.h"

#include <algorithm>
#include <chrono>

#include "../core/movegen.h"
#include "../eval/evaluation.h"
#include "movepick.h"
#include "timeman.h"

namespace albatross {

namespace {

using Clock = std::chrono::steady_clock;

struct ScoredMove {
    Move move;
    int score;
};

class Searcher {
public:
    Searcher(Position pos, const Limits& limits, std::atomic<bool>& stop, const InfoCallback& onInfo)
        : pos_(std::move(pos)), limits_(limits), stopFlag_(stop), onInfo_(onInfo) {}

    SearchResult run();

private:
    int negamax(int depth, int alpha, int beta, int ply);
    int quiescence(int alpha, int beta, int ply);
    void check_limits();
    std::int64_t elapsed_ms() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start_).count();
    }
    void update_pv(Move m, int ply) {
        pv_[ply][ply] = m;
        for (int i = ply + 1; i < pvLen_[ply + 1]; ++i) pv_[ply][i] = pv_[ply + 1][i];
        pvLen_[ply] = std::max(pvLen_[ply + 1], ply + 1);
    }
    // Picks the highest scored remaining move (selection sort: cheap because beta cutoffs come early).
    static Move pick(ScoredMove* moves, int count, int index) {
        int best = index;
        for (int i = index + 1; i < count; ++i)
            if (moves[i].score > moves[best].score) best = i;
        std::swap(moves[index], moves[best]);
        return moves[index].move;
    }

    Position pos_;
    Limits limits_;
    std::atomic<bool>& stopFlag_;
    const InfoCallback& onInfo_;

    Clock::time_point start_ = Clock::now();
    TimeBudget budget_;
    bool stopped_ = false;
    std::uint64_t nodes_ = 0;
    int seldepth_ = 0;
    Move rootPrevBest_;

    Move pv_[MAX_PLY + 1][MAX_PLY + 1];
    int pvLen_[MAX_PLY + 2] = {};
};

void Searcher::check_limits() {
    if (stopFlag_.load(std::memory_order_relaxed)) stopped_ = true;
    if (limits_.nodes && nodes_ >= limits_.nodes) stopped_ = true;
    if (budget_.hard && elapsed_ms() >= budget_.hard) stopped_ = true;
}

int Searcher::quiescence(int alpha, int beta, int ply) {
    pvLen_[ply] = ply;
    if ((++nodes_ & 2047) == 0) check_limits();
    if (stopped_) return 0;
    seldepth_ = std::max(seldepth_, ply);
    if (ply >= MAX_PLY - 1) return evaluate(pos_);

    const bool inCheck = pos_.in_check();
    int best;
    if (inCheck) {
        best = -MATE + ply;  // replaced as soon as an evasion is found
    } else {
        // "Stand pat": the side to move may decline all captures.
        best = evaluate(pos_);
        if (best >= beta) return best;
        alpha = std::max(alpha, best);
    }

    // In check every evasion is searched, otherwise only captures and promotions.
    MoveList all;
    generate_legal(pos_, all);
    ScoredMove moves[MAX_MOVES];
    int count = 0;
    for (Move m : all)
        if (inCheck || m.is_capture() || m.is_promotion()) moves[count++] = {m, score_move(pos_, m)};

    for (int i = 0; i < count; ++i) {
        const Move m = pick(moves, count, i);
        pos_.do_move(m);
        const int score = -quiescence(-beta, -alpha, ply + 1);
        pos_.undo_move();
        if (stopped_) return 0;
        if (score > best) {
            best = score;
            if (score > alpha) {
                alpha = score;
                update_pv(m, ply);
                if (alpha >= beta) break;
            }
        }
    }
    return best;
}

int Searcher::negamax(int depth, int alpha, int beta, int ply) {
    pvLen_[ply] = ply;
    if (depth <= 0) return quiescence(alpha, beta, ply);

    if ((++nodes_ & 2047) == 0) check_limits();
    if (stopped_) return 0;
    seldepth_ = std::max(seldepth_, ply);
    if (ply >= MAX_PLY - 1) return evaluate(pos_);

    if (ply > 0 && (pos_.repetition_count() >= 2 || pos_.insufficient_material())) return 0;

    MoveList list;
    generate_legal(pos_, list);
    if (list.empty()) return pos_.in_check() ? -MATE + ply : 0;  // mate (prefer the shortest) or stalemate
    if (ply > 0 && pos_.rule50() >= 100) return 0;

    ScoredMove moves[MAX_MOVES];
    int count = 0;
    for (Move m : list) {
        int s = score_move(pos_, m);
        if (ply == 0 && m == rootPrevBest_) s = 1'000'000;  // previous iteration's best move first
        moves[count++] = {m, s};
    }

    int best = -INFINITE_SCORE;
    for (int i = 0; i < count; ++i) {
        const Move m = pick(moves, count, i);
        pos_.do_move(m);
        const int score = -negamax(depth - 1, -beta, -alpha, ply + 1);
        pos_.undo_move();
        if (stopped_) return 0;
        if (score > best) {
            best = score;
            if (score > alpha) {
                alpha = score;
                update_pv(m, ply);
                if (alpha >= beta) break;
            }
        }
    }
    return best;
}

SearchResult Searcher::run() {
    start_ = Clock::now();
    budget_ = allocate_time(limits_, pos_.side_to_move());

    SearchResult result;
    MoveList rootMoves;
    generate_legal(pos_, rootMoves);
    if (rootMoves.empty()) {
        // Checkmate or stalemate: report it like other engines do ("info depth 0 score mate 0" / "cp 0").
        result.score = pos_.in_check() ? -MATE : 0;
        if (onInfo_) {
            SearchInfo info;
            info.score = result.score;
            onInfo_(info);
        }
        return result;
    }
    result.best = rootMoves[0];  // fallback if even depth 1 is interrupted

    constexpr int MaxDepth = MAX_PLY - 2;
    const int maxDepth = limits_.depth > 0 ? std::min(limits_.depth, MaxDepth) : MaxDepth;

    for (int depth = 1; depth <= maxDepth; ++depth) {
        seldepth_ = 0;
        const int score = negamax(depth, -INFINITE_SCORE, INFINITE_SCORE, 0);
        if (stopped_) break;  // an interrupted iteration is incomplete: keep the previous result

        result.score = score;
        result.depth = depth;
        result.best = pv_[0][0];
        result.pv.assign(pv_[0], pv_[0] + pvLen_[0]);
        result.nodes = nodes_;
        rootPrevBest_ = result.best;

        if (onInfo_) {
            SearchInfo info;
            info.depth = depth;
            info.seldepth = seldepth_;
            info.score = score;
            info.nodes = nodes_;
            info.timeMs = elapsed_ms();
            info.pv = result.pv;
            onInfo_(info);
        }

        // The next iteration usually takes at least as long as everything so far, so stop early
        // rather than start one that would be cut off.
        if (budget_.soft && elapsed_ms() * 2 >= budget_.soft) break;
        if (limits_.nodes && nodes_ >= limits_.nodes) break;
    }
    result.nodes = nodes_;

    // "go infinite" must not report a result before "stop", even if the depth limit was reached.
    if (limits_.infinite)
        while (!stopFlag_.load(std::memory_order_relaxed)) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return result;
}

}  // namespace

SearchResult search_position(Position pos, const Limits& limits, std::atomic<bool>& stop, const InfoCallback& onInfo) {
    Searcher s(std::move(pos), limits, stop, onInfo);
    return s.run();
}

Search::~Search() {
    stop();
    wait();
}

void Search::go(Position pos, const Limits& limits, InfoCallback onInfo, BestMoveCallback onBest) {
    stop();
    wait();
    stop_ = false;
    infinite_ = limits.infinite;
    thread_ = std::thread([this, pos = std::move(pos), limits, onInfo = std::move(onInfo), onBest = std::move(onBest)]() mutable {
        const SearchResult r = search_position(std::move(pos), limits, stop_, onInfo);
        if (onBest) onBest(r);
    });
}

void Search::stop() { stop_ = true; }

void Search::wait() {
    if (thread_.joinable()) thread_.join();
}

}  // namespace albatross
