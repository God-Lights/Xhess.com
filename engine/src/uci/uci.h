#pragma once

#include <iosfwd>
#include <string>

#include "../core/position.h"

namespace albatross {

// UCI front end. Reads commands from `in`, writes responses to `out`, so it can be unit-tested with stringstreams.
class Uci {
public:
    Uci();
    void loop(std::istream& in, std::ostream& out);
    // Returns false when the engine should quit.
    bool execute(const std::string& line, std::ostream& out);

    static constexpr const char* NAME = "Albatross 0.1.0-genesis";
    static constexpr const char* AUTHOR = "Xhess";

private:
    void cmd_position(std::istream& args, std::ostream& out);
    void cmd_go(std::istream& args, std::ostream& out);

    Position pos_;
};

}  // namespace albatross
