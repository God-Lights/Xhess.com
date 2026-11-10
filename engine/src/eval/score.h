#pragma once
// A middlegame/endgame pair of values, the unit every evaluation term is expressed in.

namespace albatross {

struct Score {
    int mg = 0;
    int eg = 0;
    Score& operator+=(Score o) { mg += o.mg; eg += o.eg; return *this; }
    Score& operator-=(Score o) { mg -= o.mg; eg -= o.eg; return *this; }
};
constexpr Score S(int mg, int eg) { return Score{mg, eg}; }
inline constexpr Score operator*(Score s, int k) { return Score{s.mg * k, s.eg * k}; }

}  // namespace albatross
