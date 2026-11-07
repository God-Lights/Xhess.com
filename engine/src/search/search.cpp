#include "search.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>

#include "../core/movegen.h"
#include "../eval/evaluation.h"
#include "movepick.h"
#include "see.h"
#include "timeman.h"

namespace albatross {

namespace {

using Clock = std::chrono::steady_clock;

struct ScoredMove {
    Move move;
    int score;
};

// Late-move reduction table: reduction grows with depth and with the move's position in the ordering.
struct LmrTable {
    int r[64][64];
    LmrTable() {
        for (int d = 1; d < 64; ++d)
            for (int i = 1; i < 64; ++i) r[d][i] = int(0.75 + std::log(double(d)) * std::log(double(i)) / 2.25);
        for (int i = 0; i < 64; ++i) r[0][i] = r[i][0] = 0;
    }
};
const LmrTable Lmr;

constexpr int HistoryMax = 16384;

class Searcher {
public:
    Searcher(Position pos, const Limits& limits, std::atomic<bool>& stop, TranspositionTable& tt,
             const InfoCallback& onInfo)
        : pos_(std::move(pos)), limits_(limits), stopFlag_(stop), tt_(tt), onInfo_(onInfo) {}

    SearchResult run();

private:
    int negamax(int depth, int alpha, int beta, int ply, bool allowNull);
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

    bool capture_is_good(Move m) const;
    int order_score(Move m, Move ttMove, int ply) const;
    void update_history(Color us, Move m, int bonus) {
        int& h = history_[us][m.from()][m.to()];
        h += bonus - h * std::abs(bonus) / HistoryMax;
    }
    void on_quiet_cutoff(Move best, const Move* quietsTried, int quietCount, int depth, int ply);

    Position pos_;
    Limits limits_;
    std::atomic<bool>& stopFlag_;
    TranspositionTable& tt_;
    const InfoCallback& onInfo_;

    Clock::time_point start_ = Clock::now();
    TimeBudget budget_;
    bool stopped_ = false;
    std::uint64_t nodes_ = 0;
    int seldepth_ = 0;
    Move rootPrevBest_;
    std::vector<Move> allowedRoot_;   // "searchmoves" restriction (empty = all legal moves)
    std::vector<Move> excludedRoot_;  // best moves of the MultiPV lines already searched at this depth

    Move killers_[MAX_PLY + 2][2] = {};
    int history_[COLOR_NB][SQUARE_NB][SQUARE_NB] = {};

    Move pv_[MAX_PLY + 1][MAX_PLY + 1];
    int pvLen_[MAX_PLY + 2] = {};
};

void Searcher::check_limits() {
    if (stopFlag_.load(std::memory_order_relaxed)) stopped_ = true;
    if (limits_.nodes && nodes_ >= limits_.nodes) stopped_ = true;
    if (budget_.hard && elapsed_ms() >= budget_.hard) stopped_ = true;
}

// A capture is "good" if it cannot lose material: victim at least as valuable as the attacker, or SEE >= 0.
bool Searcher::capture_is_good(Move m) const {
    const PieceType victim = m.flag() == EP_CAPTURE ? PAWN : type_of(pos_.piece_on(m.to()));
    const PieceType attacker = type_of(pos_.piece_on(m.from()));
    if (PieceValue[victim] >= PieceValue[attacker]) return true;
    return see(pos_, m) >= 0;
}

int Searcher::order_score(Move m, Move ttMove, int ply) const {
    if (m == ttMove) return 3'000'000;
    if (m.is_capture() || (m.is_promotion() && m.promotion_type() == QUEEN)) {
        const bool good = m.is_promotion() || capture_is_good(m);
        return (good ? 2'000'000 : -1'000'000) + score_move(pos_, m);
    }
    if (m.is_promotion()) return -3'000'000 + PieceValue[m.promotion_type()];  // under-promotions last
    if (m == killers_[ply][0]) return 1'900'000;
    if (m == killers_[ply][1]) return 1'800'000;
    return history_[pos_.side_to_move()][m.from()][m.to()];
}

// A quiet move caused a beta cutoff: remember it as a killer and reward it in the history table;
// quiet moves tried before it (which did not cut off) are penalised.
void Searcher::on_quiet_cutoff(Move best, const Move* quietsTried, int quietCount, int depth, int ply) {
    if (killers_[ply][0] != best) {
        killers_[ply][1] = killers_[ply][0];
        killers_[ply][0] = best;
    }
    const Color us = pos_.side_to_move();
    const int bonus = std::min(depth * depth, 1600);
    update_history(us, best, bonus);
    for (int i = 0; i < quietCount; ++i)
        if (quietsTried[i] != best) update_history(us, quietsTried[i], -bonus);
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
    for (Move m : all) {
        if (inCheck) {
            moves[count++] = {m, score_move(pos_, m)};
        } else if (m.is_capture() || m.is_promotion()) {
            // Captures that lose material by SEE cannot improve on standing pat in practice.
            if (m.is_capture() && !m.is_promotion() && !capture_is_good(m)) continue;
            moves[count++] = {m, score_move(pos_, m)};
        }
    }

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

int Searcher::negamax(int depth, int alpha, int beta, int ply, bool allowNull) {
    pvLen_[ply] = ply;
    const bool inCheck = pos_.in_check();
    if (inCheck) ++depth;  // check extension: never drop into quiescence while in check
    if (depth <= 0) return quiescence(alpha, beta, ply);

    if ((++nodes_ & 2047) == 0) check_limits();
    if (stopped_) return 0;
    seldepth_ = std::max(seldepth_, ply);
    if (ply >= MAX_PLY - 1) return evaluate(pos_);

    const bool root = ply == 0;
    const bool pvNode = beta - alpha > 1;
    if (!root && (pos_.repetition_count() >= 2 || pos_.insufficient_material())) return 0;

    // Transposition table: reuse an earlier result of at least this depth (never in PV nodes, to keep the PV intact).
    const TTProbe tte = tt_.probe(pos_.key(), ply);
    Move ttMove = tte.found ? tte.move : Move::none();
    if (!root && !pvNode && tte.found && tte.depth >= depth) {
        if (tte.bound == BOUND_EXACT || (tte.bound == BOUND_LOWER && tte.score >= beta) ||
            (tte.bound == BOUND_UPPER && tte.score <= alpha))
            return tte.score;
    }

    const Color us = pos_.side_to_move();

    // Null-move pruning: if passing the turn still fails high, a real move will too. Not in check, not with only
    // king and pawns (zugzwang), and never twice in a row.
    if (!pvNode && !inCheck && allowNull && depth >= 3 && pos_.has_non_pawn_material(us)) {
        const int staticEval = evaluate(pos_);
        if (staticEval >= beta) {
            const int R = 3 + depth / 4 + std::min((staticEval - beta) / 200, 2);
            pos_.do_null_move();
            const int score = -negamax(depth - 1 - R, -beta, -beta + 1, ply + 1, false);
            pos_.undo_null_move();
            if (stopped_) return 0;
            if (score >= beta) return score >= MATE_IN_MAX_PLY ? beta : score;  // do not trust unproven mates
        }
    }

    MoveList list;
    generate_legal(pos_, list);
    if (list.empty()) return inCheck ? -MATE + ply : 0;  // mate (prefer the shortest) or stalemate
    if (!root && pos_.rule50() >= 100) return 0;

    ScoredMove moves[MAX_MOVES];
    int count = 0;
    for (Move m : list) {
        if (root) {
            if (!allowedRoot_.empty() && std::find(allowedRoot_.begin(), allowedRoot_.end(), m) == allowedRoot_.end()) continue;
            if (std::find(excludedRoot_.begin(), excludedRoot_.end(), m) != excludedRoot_.end()) continue;
        }
        int s = order_score(m, ttMove, ply);
        if (root && m == rootPrevBest_) s = 4'000'000;  // previous iteration's best move first
        moves[count++] = {m, s};
    }

    const int origAlpha = alpha;
    int best = -INFINITE_SCORE;
    Move bestMove;
    Move quietsTried[MAX_MOVES];
    int quietCount = 0;

    for (int i = 0; i < count; ++i) {
        const Move m = pick(moves, count, i);
        const bool quiet = !m.is_capture() && !m.is_promotion();
        pos_.do_move(m);
        const bool givesCheck = pos_.in_check();
        const int newDepth = depth - 1;

        int score;
        if (i == 0) {
            score = -negamax(newDepth, -beta, -alpha, ply + 1, true);
        } else {
            // Late move reductions: later quiet moves are probably bad, so look at them with less depth first.
            int reduction = 0;
            if (depth >= 3 && i >= 3 && quiet && !inCheck && !givesCheck) {
                reduction = Lmr.r[std::min(depth, 63)][std::min(i, 63)];
                if (pvNode) --reduction;
                if (m == killers_[ply][0] || m == killers_[ply][1]) --reduction;
                reduction = std::clamp(reduction, 0, newDepth - 1);
            }
            // Principal variation search: null window first, full window only if it unexpectedly beats alpha.
            score = -negamax(newDepth - reduction, -alpha - 1, -alpha, ply + 1, true);
            if (score > alpha && reduction > 0) score = -negamax(newDepth, -alpha - 1, -alpha, ply + 1, true);
            if (score > alpha && score < beta) score = -negamax(newDepth, -beta, -alpha, ply + 1, true);
        }
        pos_.undo_move();
        if (stopped_) return 0;

        if (score > best) {
            best = score;
            bestMove = m;
            if (score > alpha) {
                alpha = score;
                update_pv(m, ply);
                if (alpha >= beta) {
                    if (quiet) on_quiet_cutoff(m, quietsTried, quietCount, depth, ply);
                    break;
                }
            }
        }
        if (quiet) quietsTried[quietCount++] = m;
    }

    const Bound bound = best >= beta ? BOUND_LOWER : (best > origAlpha ? BOUND_EXACT : BOUND_UPPER);
    tt_.store(pos_.key(), depth, best, bound, bestMove, ply);
    return best;
}

SearchResult Searcher::run() {
    start_ = Clock::now();
    budget_ = allocate_time(limits_, pos_.side_to_move());
    tt_.new_search();

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
    // "searchmoves": keep only the legal moves from the list; ignore the restriction if none of them is legal.
    for (Move m : limits_.searchMoves)
        if (std::find(rootMoves.begin(), rootMoves.end(), m) != rootMoves.end()) allowedRoot_.push_back(m);
    const int candidates = allowedRoot_.empty() ? int(rootMoves.size()) : int(allowedRoot_.size());
    const int lineCount = std::clamp(limits_.multiPv, 1, std::max(1, candidates));
    result.best = allowedRoot_.empty() ? rootMoves[0] : allowedRoot_[0];  // fallback if even depth 1 is interrupted

    constexpr int MaxDepth = MAX_PLY - 2;
    const int maxDepth = limits_.depth > 0 ? std::min(limits_.depth, MaxDepth) : MaxDepth;
    std::vector<Move> prevBest(lineCount);

    for (int depth = 1; depth <= maxDepth; ++depth) {
        seldepth_ = 0;
        std::vector<RootLine> lines;
        excludedRoot_.clear();
        bool complete = true;
        for (int k = 0; k < lineCount; ++k) {
            rootPrevBest_ = prevBest[k];
            const int score = negamax(depth, -INFINITE_SCORE, INFINITE_SCORE, 0, false);
            if (stopped_) {
                complete = false;
                break;
            }
            RootLine line;
            line.score = score;
            line.pv.assign(pv_[0], pv_[0] + pvLen_[0]);
            excludedRoot_.push_back(line.pv[0]);  // the next line must start with a different move
            lines.push_back(std::move(line));
        }
        if (!complete) break;  // an interrupted iteration is incomplete: keep the previous result

        // The lines were searched one after another with the earlier best moves excluded, so a later line can
        // end up with a higher score than an earlier one (search instability). Report them best first.
        std::stable_sort(lines.begin(), lines.end(), [](const RootLine& a, const RootLine& b) { return a.score > b.score; });
        for (int k = 0; k < lineCount; ++k) prevBest[k] = lines[k].pv[0];
        result.score = lines[0].score;
        result.depth = depth;
        result.best = lines[0].pv[0];
        result.pv = lines[0].pv;
        result.lines = lines;
        result.nodes = nodes_;

        if (onInfo_) {
            for (int k = 0; k < lineCount; ++k) {
                SearchInfo info;
                info.multipv = k + 1;
                info.depth = depth;
                info.seldepth = seldepth_;
                info.score = lines[k].score;
                info.nodes = nodes_;
                info.timeMs = elapsed_ms();
                info.hashfull = tt_.hashfull();
                info.pv = lines[k].pv;
                onInfo_(info);
            }
        }

        // The next iteration usually takes at least as long as everything so far, so stop early
        // rather than start one that would be cut off.
        if (budget_.soft && elapsed_ms() * 2 >= budget_.soft) break;
        if (limits_.nodes && nodes_ >= limits_.nodes) break;
    }
    result.nodes = nodes_;

    // "go infinite" must not report a result before "stop", even if the depth limit was reached.
#ifndef ALBATROSS_NO_THREADS
    if (limits_.infinite)
        while (!stopFlag_.load(std::memory_order_relaxed)) std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
    return result;
}

}  // namespace

SearchResult search_position(Position pos, const Limits& limits, std::atomic<bool>& stop, TranspositionTable& tt,
                             const InfoCallback& onInfo) {
    Searcher s(std::move(pos), limits, stop, tt, onInfo);
    return s.run();
}

SearchResult search_position(Position pos, const Limits& limits, std::atomic<bool>& stop, const InfoCallback& onInfo) {
    TranspositionTable tt(8);
    return search_position(std::move(pos), limits, stop, tt, onInfo);
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
#ifdef ALBATROSS_NO_THREADS
    // Single-threaded build: search right here. "stop" cannot arrive while this runs.
    const SearchResult r = search_position(std::move(pos), limits, stop_, tt_, onInfo);
    if (onBest) onBest(r);
#else
    thread_ = std::thread([this, pos = std::move(pos), limits, onInfo = std::move(onInfo), onBest = std::move(onBest)]() mutable {
        const SearchResult r = search_position(std::move(pos), limits, stop_, tt_, onInfo);
        if (onBest) onBest(r);
    });
#endif
}

void Search::stop() { stop_ = true; }

void Search::wait() {
#ifndef ALBATROSS_NO_THREADS
    if (thread_.joinable()) thread_.join();
#endif
}

}  // namespace albatross
