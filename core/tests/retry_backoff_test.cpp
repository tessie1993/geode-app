// Host test for core/audio/output/RetryBackoff: when the native playback output is tried again after a failure.
//
// Build and run (one command):
//   g++ -std=c++20 -O1 -Wall -Wextra -Werror -Icore core/tests/retry_backoff_test.cpp -o retry_backoff_test && ./retry_backoff_test
#include <cstdint>
#include <cstdio>

#include "audio/output/RetryBackoff.hpp"

using geode::audio::output::RetryBackoff;

static int failures = 0;
#define CHECK(cond, msg)                                        \
    do {                                                        \
        if (!(cond)) {                                          \
            std::printf("FAIL: %s (line %d)\n", msg, __LINE__); \
            ++failures;                                         \
        }                                                       \
    } while (0)

int main() {
    const int64_t window = RetryBackoff::kRetryAfterMs;

    {
        RetryBackoff b;
        CHECK(b.mayOpen(0), "no failure yet: open at once, even at clock zero");
        CHECK(b.mayOpen(93'000), "no failure yet: open at any time");
    }
    {
        RetryBackoff b;
        b.noteFailed(141'000);
        CHECK(!b.mayOpen(141'000), "closed the instant it failed");
        CHECK(!b.mayOpen(141'000 + window - 1), "closed one millisecond before the window ends");
        CHECK(b.mayOpen(141'000 + window), "open when the window has passed");
        CHECK(b.mayOpen(141'000 + 3 * window), "open long after");
    }
    {
        RetryBackoff b;
        b.noteFailed(0);
        CHECK(!b.mayOpen(window - 1), "a failure at clock zero still closes the window");
        CHECK(b.mayOpen(window), "and it ends");
    }
    {
        RetryBackoff b;
        b.noteFailed(77'000);
        b.noteFailed(77'000 + window + 5'000);
        CHECK(!b.mayOpen(77'000 + window + 6'000), "a second failure restarts the window");
        CHECK(b.mayOpen(77'000 + 2 * window + 5'000), "and it ends sixty seconds after the second");
    }

    if (failures == 0) std::printf("retry_backoff_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
