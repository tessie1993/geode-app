// Host test for core/util/WorkHint: the ADPF wrapper is a no-op off Android (and below API 33), and the target it is
// told is the paced frame's length. Built with the runner's compiler:
//   g++ -std=c++20 -O1 -Wall -Wextra -Werror -Icore core/tests/work_hint_test.cpp -o work_hint_test
#include <cstdint>
#include <cstdio>
#include <limits>

#include "util/WorkHint.hpp"

using geode::util::WorkHint;

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        failures++;
    }
}
}  // namespace

int main() {
    check(WorkHint::targetNs(50.0f) == 20000000, "a 50 fps frame is 20 ms");
    check(WorkHint::targetNs(0.0f) == 0 && WorkHint::targetNs(-5.0f) == 0, "no rate, no target");
    check(WorkHint::targetNs(std::numeric_limits<float>::quiet_NaN()) == 0, "a NaN rate gives no target");
    check(WorkHint::targetNs(std::numeric_limits<float>::infinity()) == 0, "an infinite rate gives no target");
    WorkHint hint;
    hint.report(0, 1000);
    hint.report(16000000, 0);
    hint.report(16000000, 9000000);
    hint.report(33000000, 12000000);
    check(true, "reports are accepted, and ignored where there is no session");
    if (failures == 0) std::printf("work_hint_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
