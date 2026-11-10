#include "params.h"

#ifdef ALBATROSS_TUNABLE

#include <cstdlib>
#include <map>
#include <sstream>

namespace albatross {

EvalParams EvalP;

const std::vector<TunableParam>& tunable_params() {
    static const std::vector<TunableParam> all = [] {
        std::vector<TunableParam> v;
#define SCORE(name, mgValue, egValue) v.push_back({#name ".mg", &EvalP.name.mg}); v.push_back({#name ".eg", &EvalP.name.eg});
#define INT(name, value) v.push_back({#name, &EvalP.name});
#define ARRAY(name, size, ...) for (int i = 0; i < size; ++i) v.push_back({std::string(#name) + "[" + std::to_string(i) + "]", &EvalP.name[i]});
#include "params.def"
#undef SCORE
#undef INT
#undef ARRAY
        return v;
    }();
    return all;
}

bool apply_overrides(const std::string& spec, std::string* error) {
    static const std::map<std::string, int*> byName = [] {
        std::map<std::string, int*> m;
        for (const TunableParam& p : tunable_params()) m[p.name] = p.value;
        return m;
    }();
    std::vector<std::pair<int*, int>> changes;
    std::stringstream ss(spec);
    for (std::string item; std::getline(ss, item, ',');) {
        if (item.empty()) continue;
        const std::size_t eq = item.find('=');
        auto fail = [&](const std::string& why) {
            if (error) *error = why + ": " + item;
            return false;
        };
        if (eq == std::string::npos) return fail("expected name=value");
        const auto it = byName.find(item.substr(0, eq));
        if (it == byName.end()) return fail("unknown parameter");
        char* end = nullptr;
        const std::string value = item.substr(eq + 1);
        const long v = std::strtol(value.c_str(), &end, 10);
        if (value.empty() || *end != '\0') return fail("not an integer");
        changes.push_back({it->second, int(v)});
    }
    for (auto& [ptr, v] : changes) *ptr = v;
    return true;
}

std::vector<int> params_snapshot() {
    std::vector<int> out;
    for (const TunableParam& p : tunable_params()) out.push_back(*p.value);
    return out;
}

void params_restore(const std::vector<int>& values) {
    const auto& all = tunable_params();
    for (std::size_t i = 0; i < all.size() && i < values.size(); ++i) *all[i].value = values[i];
}

}  // namespace albatross

#endif
