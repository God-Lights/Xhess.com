#pragma once
// Static evaluation. Search only depends on this interface, so a different evaluator (NNUE later)
// can replace classical.cpp without touching the search.

#include <string>
#include <vector>

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

// ---- Strategic themes: which squares earned (or cost) points, and how many ----
// Display-only: collecting them never changes the score. Every mark belongs to a side and carries that side's
// signed contribution (positive = asset of that side, negative = liability of that side).

enum Theme : int {
    THEME_PASSED,        // passed pawn
    THEME_UNSTOPPABLE,   // passed pawn the enemy king cannot catch (rule of the square)
    THEME_DOUBLED,       // doubled pawn
    THEME_ISOLATED,      // isolated pawn
    THEME_OUTPOST,       // knight on a square no enemy pawn can attack
    THEME_ROOK_OPEN,     // rook on an open file
    THEME_ROOK_SEMI,     // rook on a half-open file
    THEME_ROOK_SEVENTH,  // rook on the 7th rank
    THEME_BISHOP_PAIR,   // both bishops
    THEME_KING_SHIELD,   // missing pawn shield / open file next to the king (squares = holes in front of the king)
    THEME_KING_DANGER,   // enemy pieces bearing down on the king zone (squares = the attackers)
    THEME_INACTIVE,      // knight/bishop/rook with almost no safe squares
    THEME_MOPUP,         // a lone enemy king being driven to the edge (square = that king)
    THEME_NB
};

struct EvalMark {
    Theme theme;
    Color color;
    Square square;   // the square that earned/cost the points (SQ_NONE if none)
    int mg;
    int eg;
};

struct EvalTrace {
    TermScore term[TERM_NB][COLOR_NB];
    int phase = 0;        // 24 = full middlegame, 0 = pure endgame
    int egScale = 128;    // 128 = no scaling; < 128 for drawish endgames (opposite-colored bishops)
    int white = 0;        // blended score from White's point of view
    int stm = 0;          // the same score from the side to move (what evaluate() returns)
    std::vector<EvalMark> marks;   // filled by evaluate_trace only
};

EvalTrace evaluate_trace(const Position& pos);
std::string format_trace(const EvalTrace& t);

const char* term_name(EvalTerm t);

// One theme of one side: its squares and its value (centipawns, blended with the position's phase).
struct ThemeSummary {
    Theme theme;
    Color color;
    std::vector<Square> squares;
    int mg = 0;
    int eg = 0;
    int cp = 0;
};

// Aggregates the marks of a trace into one entry per (theme, side).
std::vector<ThemeSummary> summarize_themes(const EvalTrace& t);

const char* theme_id(Theme t);   // "passed", "king_shield", ... (the names used by the UCI "themes" command)

// UCI "themes" output: one line per (theme, side):  theme <id> <white|black> <squares,comma,separated|-> cp <n> mg <n> eg <n>
// followed by:  themes done phase <n> scale <n> white <cp>
std::string format_themes(const EvalTrace& t);

}  // namespace albatross
