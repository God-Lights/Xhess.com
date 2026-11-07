#pragma once
// Static evaluation. Search only depends on this interface, so a different evaluator (NNUE later)
// can replace classical.cpp without touching the search.

#include <string>

#include "../core/position.h"

namespace albatross {

// Centipawns, indexed by PieceType. The king has no material value. Search ordering and SEE use these;
// the evaluator has its own middlegame/endgame material values.
constexpr int PieceValue[PIECE_TYPE_NB] = {0, 100, 320, 330, 500, 900, 0};

// Score in centipawns from the point of view of the side to move.
int evaluate(const Position& pos);

// ---- Evaluation breakdown (UCI "eval" command; later the source of strategy themes) ----

enum EvalTerm : int {
    TERM_MATERIAL,    // piece values
    TERM_PST,         // piece-square tables (king included)
    TERM_IMBALANCE,   // bishop pair
    TERM_PAWNS,       // doubled, isolated, supported/phalanx pawns
    TERM_PASSED,      // passed pawns (rank, blockers, king proximity)
    TERM_MOBILITY,    // safe squares of knights, bishops, rooks, queens
    TERM_KNIGHTS,     // outposts
    TERM_ROOKS,       // open/semi-open files, 7th rank
    TERM_KING,        // pawn shield, open files near the king, attackers on the king zone
    TERM_ENDGAME,     // mop-up (mating a lone king), unstoppable passed pawns
    TERM_NB
};

struct TermScore {
    int mg = 0;
    int eg = 0;
};

struct EvalTrace {
    TermScore term[TERM_NB][COLOR_NB];
    int phase = 0;        // 24 = full middlegame, 0 = pure endgame
    int egScale = 128;    // 128 = no scaling; < 128 for drawish endgames (opposite-colored bishops)
    int white = 0;        // blended score from White's point of view
    int stm = 0;          // the same score from the side to move (what evaluate() returns)
};

EvalTrace evaluate_trace(const Position& pos);
std::string format_trace(const EvalTrace& t);

const char* term_name(EvalTerm t);

}  // namespace albatross
