// Host test for cast/CastWatch through its C ABI: a stream waits up to fifteen seconds to be ready, then ends on
// five seconds without a segment or on losing its address; the poll cadence; the fallback renderer freed after
// thirty seconds of the stage leading.
//
// Build & run:
//   g++ -std=c++20 -O1 -Wall -Wextra -Werror -Icore -Icore/tests/stub core/tests/cast_watch_test.cpp
//       core/api/geode_cast_watch_api.cpp -o cast_watch_test && ./cast_watch_test
#include <cstdint>
#include <cstdio>

#include "api/geode_api.h"

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

}  // namespace

int main() {
    const int64_t start = 7'000;
    // Waiting: ready wins, then the timeout, which is strictly after fifteen seconds.
    check(geode_cast_watch(0, start + 100, start, 0, 0, 0) == GEODE_CAST_WATCH_WAIT, "waits");
    check(geode_cast_watch(0, start + 100, start, 1, 0, 0) == GEODE_CAST_WATCH_READY, "ready");
    check(geode_cast_watch(0, start + 15'000, start, 0, 0, 0) == GEODE_CAST_WATCH_WAIT, "fifteen seconds is not late");
    check(geode_cast_watch(0, start + 15'001, start, 0, 0, 0) == GEODE_CAST_WATCH_START_TIMEOUT, "past it is");
    check(geode_cast_watch(0, start + 40'000, start, 1, 9'000, 0) == GEODE_CAST_WATCH_READY,
          "ready late still counts; the address is not read before");
    // Announced: frozen first, then the address.
    check(geode_cast_watch(1, start, start, 1, 5'000, 1) == GEODE_CAST_WATCH_PLAYING, "five seconds is not frozen");
    check(geode_cast_watch(1, start, start, 1, 5'001, 1) == GEODE_CAST_WATCH_FROZEN, "past it is");
    check(geode_cast_watch(1, start, start, 1, 5'001, 0) == GEODE_CAST_WATCH_FROZEN, "frozen before the address");
    check(geode_cast_watch(1, start, start, 1, 900, 0) == GEODE_CAST_WATCH_NETWORK_GONE, "the address gone");
    check(geode_cast_watch(1, start + 99'000, start, 0, 0, 1) == GEODE_CAST_WATCH_PLAYING, "no start timeout once announced");
    // Cadence.
    check(geode_cast_watch_next_ms(0) == 250 && geode_cast_watch_next_ms(1) == 1'000, "poll a quarter second, then a second");
    // The fallback renderer.
    const int64_t drew = 3'000'000'000;
    check(geode_cast_fallback_release(drew + 30'000'000'000, drew) == 0, "thirty seconds keeps it");
    check(geode_cast_fallback_release(drew + 30'000'000'001, drew) == 1, "past it frees it");
    if (failures == 0) std::printf("cast_watch_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
