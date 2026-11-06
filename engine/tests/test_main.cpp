#include <cstring>

#include "core/init.h"
#include "test.h"

namespace testing {
std::vector<Case>& registry() {
    static std::vector<Case> r;
    return r;
}
int& failures() {
    static int f = 0;
    return f;
}
}  // namespace testing

int main(int argc, char** argv) {
    albatross::init();
    const bool slow = argc > 1 && std::strcmp(argv[1], "--slow") == 0;
    int run = 0, failed = 0;
    for (const auto& c : testing::registry()) {
        if (c.slow != slow) continue;
        const int before = testing::failures();
        std::cout << "[ RUN  ] " << c.name << std::endl;
        c.fn();
        ++run;
        if (testing::failures() != before) {
            ++failed;
            std::cout << "[ FAIL ] " << c.name << std::endl;
        }
    }
    std::cout << run - failed << "/" << run << " tests passed\n";
    return failed ? 1 : 0;
}
