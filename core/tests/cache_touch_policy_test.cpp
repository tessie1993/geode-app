// Host-side checks for when a used program binary is stamped again (core/viz/CacheTouchPolicy.hpp):
// a hit stamped within the last day is left alone (no disk write per load), an older one and a program
// still on probation are stamped. No GL, no files. Built and run by CI ("Native host tests").
#include <chrono>
#include <cstdio>
#include <filesystem>

#include "viz/CacheTouchPolicy.hpp"

using geode::viz::touchDue;
using Dur = std::filesystem::file_time_type::duration;

static int failures = 0;
#define CHECK(c)                                              \
    do {                                                      \
        if (!(c)) {                                           \
            std::printf("FAIL line %d: %s\n", __LINE__, #c);  \
            failures++;                                       \
        }                                                     \
    } while (0)

int main() {
    using namespace std::chrono;
    CHECK(!touchDue(false, duration_cast<Dur>(seconds(5))));
    CHECK(!touchDue(false, duration_cast<Dur>(hours(23))));
    CHECK(touchDue(false, duration_cast<Dur>(hours(24))));
    CHECK(touchDue(false, duration_cast<Dur>(hours(24 * 30))));
    CHECK(touchDue(true, duration_cast<Dur>(seconds(1))));
    CHECK(touchDue(false, duration_cast<Dur>(seconds(-5))));  // a clock that went back: stamp it again
    if (failures == 0) std::printf("cache_touch_policy_test: ok\n");
    return failures == 0 ? 0 : 1;
}
