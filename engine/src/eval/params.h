#pragma once
// Evaluation weights. The values live in params.def; this header turns them into a struct.
//
//   normal build   EvalP is a constexpr object, so the compiler folds the weights into the code: no cost at all.
//   tunable build  (-DALBATROSS_TUNABLE: tools/tuner and albatross_tunable) EvalP is a mutable global with a registry
//                  of all parameters, so a tuner can change them and the UCI option EvalOverride can set them.

#include <string>
#include <vector>

#include "score.h"

namespace albatross {

struct EvalParams {
#define SCORE(name, mgValue, egValue) Score name{mgValue, egValue};
#define INT(name, value) int name = value;
#define ARRAY(name, size, ...) int name[size] = {__VA_ARGS__};
#include "params.def"
#undef SCORE
#undef INT
#undef ARRAY
};

#ifdef ALBATROSS_TUNABLE

extern EvalParams EvalP;

struct TunableParam {
    std::string name;   // "doubledPawn.mg", "shieldNear", "pstKnight[27]"
    int* value;
};

// Every integer of EvalParams, one entry per element (arrays are expanded).
const std::vector<TunableParam>& tunable_params();

// Sets parameters from "name=value,name=value" (names as in tunable_params()). Returns false and fills `error`
// for an unknown name or a malformed value; nothing is changed in that case.
bool apply_overrides(const std::string& spec, std::string* error = nullptr);

// Snapshot / restore of all values (the tuner and the tests use these).
std::vector<int> params_snapshot();
void params_restore(const std::vector<int>& values);

#else

inline constexpr EvalParams EvalP{};

#endif

}  // namespace albatross
