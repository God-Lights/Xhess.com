// WebAssembly entry point: a tiny C API around the UCI front end.
//
//   albatross_init()            builds the attack tables (idempotent)
//   albatross_command("go ...") runs one UCI command line; output lines are delivered to Module.onLine(line)
//
// The build is single-threaded (ALBATROSS_NO_THREADS): "go" blocks until the search is done. The JavaScript
// driver (wasm/driver.js) runs this module in a dedicated worker and stops a search by restarting that worker.

#include <emscripten.h>

#include <sstream>
#include <streambuf>
#include <string>

#include "core/init.h"
#include "uci/uci.h"

// Hands one output line to JavaScript.
EM_JS(void, albatross_emit_line, (const char* s), {
    if (Module.onLine) Module.onLine(UTF8ToString(s));
});

namespace {

// Splits everything the UCI layer writes into lines and forwards each one to JavaScript.
class LineBuf : public std::streambuf {
protected:
    int_type overflow(int_type c) override {
        if (c == traits_type::eof()) return traits_type::not_eof(c);
        if (c == '\n') {
            if (!line_.empty()) albatross_emit_line(line_.c_str());
            line_.clear();
        } else if (c != '\r') {
            line_.push_back(static_cast<char>(c));
        }
        return c;
    }

private:
    std::string line_;
};

LineBuf lineBuf;
std::ostream out(&lineBuf);
albatross::Uci uci;

}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE void albatross_init() { albatross::init(); }

EMSCRIPTEN_KEEPALIVE void albatross_command(const char* line) { uci.execute(line, out); }

}  // extern "C"

int main() { return 0; }  // the module is driven through the C API above
