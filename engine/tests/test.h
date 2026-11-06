#pragma once
// Minimal test harness: no external dependencies.

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace testing {

struct Case {
    const char* name;
    void (*fn)();
    bool slow;
};

std::vector<Case>& registry();
int& failures();

struct Registrar {
    Registrar(const char* name, void (*fn)(), bool slow) { registry().push_back({name, fn, slow}); }
};

}  // namespace testing

#define TEST(name)                                                  \
    static void test_##name();                                      \
    static testing::Registrar reg_##name(#name, test_##name, false); \
    static void test_##name()

#define TEST_SLOW(name)                                             \
    static void test_##name();                                      \
    static testing::Registrar reg_##name(#name, test_##name, true);  \
    static void test_##name()

#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            std::cerr << "  FAIL " << __FILE__ << ":" << __LINE__ << "  " #cond "\n";  \
            ++testing::failures();                                                     \
        }                                                                              \
    } while (0)

#define CHECK_EQ(a, b)                                                                              \
    do {                                                                                            \
        const auto va = (a);                                                                        \
        const auto vb = (b);                                                                        \
        if (!(va == vb)) {                                                                          \
            std::cerr << "  FAIL " << __FILE__ << ":" << __LINE__ << "  " #a " == " #b "\n    got " \
                      << va << " expected " << vb << "\n";                                          \
            ++testing::failures();                                                                  \
        }                                                                                           \
    } while (0)
