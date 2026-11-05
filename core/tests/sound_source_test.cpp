// Host test for core/sound/SoundSource: which sound the wallpaper and the dream hear (#773).
//
// g++ -std=c++20 -O1 -Wall -Wextra -Werror -Icore -Icore/tests/stub core/tests/sound_source_test.cpp \
//     core/api/geode_sound_api.cpp -o sound_source_test && ./sound_source_test
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

#include "api/geode_api.h"
#include "sound/SoundSource.hpp"

namespace src = geode::sound::source;

static int failures = 0;
#define CHECK(cond, ...)                                    \
    do {                                                    \
        if (!(cond)) {                                      \
            std::fprintf(stderr, "FAIL %s:%d ", __FILE__, __LINE__); \
            std::fprintf(stderr, __VA_ARGS__);              \
            std::fprintf(stderr, "\n");                     \
            ++failures;                                     \
        }                                                   \
    } while (0)

namespace {

constexpr int64_t kPollMs = 250;
constexpr int64_t kRing = 7;

// The shell's side of a poll: a capture that starts when asked (unless refused) and stops when told.
struct Shell {
    src::State state;
    bool captureRunning = false;
    bool refuseStart = false;
    int starts = 0;
    int stops = 0;
    bool usingCapture = false;

    void poll(int64_t now, bool ringPresent, int64_t written, bool permitted) {
        const int out = src::step(state, now, ringPresent, kRing, written, permitted, captureRunning);
        if (out & src::kStopCapture) {
            if (captureRunning) ++stops;
            captureRunning = false;
        }
        if (out & src::kStartCapture) {
            ++starts;
            if (refuseStart) src::startFailed(state, now);
            else captureRunning = true;
        }
        usingCapture = (out & src::kUseCapture) && captureRunning;
    }
};

// A ring written on every poll keeps the source on the ring and never opens the capture.
void testFedRingWins() {
    Shell s;
    int64_t written = 0;
    for (int64_t t = 0; t < 20000; t += kPollMs) {
        written += 480;
        s.poll(t, true, written, true);
        CHECK(!s.usingCapture, "fed ring lost to capture at %lld", (long long)t);
    }
    CHECK(s.starts == 0, "capture started %d times while the ring was fed", s.starts);
}

// The ring stops: the source holds for the stall window, then falls back to the capture.
void testStallFallsBack() {
    Shell s;
    int64_t written = 0;
    int64_t t = 0;
    for (; t < 2000; t += kPollMs) s.poll(t, true, written += 480, true);
    const int64_t lastWrite = t - kPollMs;
    int64_t fellBackAt = -1;
    for (; t < 5000; t += kPollMs) {
        s.poll(t, true, written, true);
        if (s.usingCapture && fellBackAt < 0) fellBackAt = t;
    }
    CHECK(fellBackAt >= lastWrite + src::kStallMs, "fell back too early: %lld", (long long)fellBackAt);
    CHECK(fellBackAt <= lastWrite + src::kStallMs + kPollMs, "fell back too late: %lld", (long long)fellBackAt);
    CHECK(s.starts == 1, "capture started %d times", s.starts);

    // The ring comes back fed: the ring takes over after kReturnMs and the capture stops.
    int64_t back = -1;
    const int64_t resumed = t;
    for (; t < 8000; t += kPollMs) {
        s.poll(t, true, written += 480, true);
        if (!s.usingCapture && back < 0) back = t;
    }
    CHECK(back >= resumed + src::kReturnMs, "returned too early: %lld", (long long)back);
    CHECK(back <= resumed + src::kReturnMs + kPollMs, "returned too late: %lld", (long long)back);
    CHECK(s.stops == 1 && !s.captureRunning, "capture stops %d, running %d", s.stops, s.captureRunning);
}

// No ring at all: straight to the capture, when permitted.
void testNoRingUsesCapture() {
    Shell s;
    s.poll(0, false, 0, true);
    CHECK(s.usingCapture, "no ring did not use the capture");
}

// A ring written in bursts right at the stall boundary (a write every kStallMs + one poll) must not
// flap the capture: once on the capture, a lone write never pulls the source back.
void testNoFlapAtBoundary() {
    for (int64_t gap : {src::kStallMs - kPollMs, src::kStallMs, src::kStallMs + kPollMs, 2 * src::kStallMs}) {
        Shell s;
        int64_t written = 0;
        int64_t nextWrite = 0;
        int switches = 0;
        bool was = false;
        for (int64_t t = 0; t < 60000; t += kPollMs) {
            if (t >= nextWrite) {
                written += 480;
                nextWrite = t + gap;
            }
            s.poll(t, true, written, true);
            if (t > 0 && s.usingCapture != was) ++switches;
            was = s.usingCapture;
        }
        CHECK(switches <= 1, "gap %lld: source switched %d times", (long long)gap, switches);
        CHECK(s.starts <= 1, "gap %lld: capture started %d times", (long long)gap, s.starts);
    }
}

// Without permission the source stays on the ring and never asks for the capture, fed or not.
void testNoPermissionStaysOnRing() {
    Shell s;
    int64_t written = 0;
    for (int64_t t = 0; t < 10000; t += kPollMs) {
        if (t < 2000) written += 480;
        s.poll(t, true, written, false);
        CHECK(!s.usingCapture, "used the capture without permission at %lld", (long long)t);
    }
    CHECK(s.starts == 0, "capture started %d times without permission", s.starts);

    // Revoked while capturing: back to the ring and the capture stops.
    Shell r;
    r.poll(0, false, 0, true);
    CHECK(r.usingCapture, "capture not used");
    r.poll(kPollMs, false, 0, false);
    CHECK(!r.usingCapture && !r.captureRunning, "revoked capture kept running");
}

// A refused start backs off for kRetryMs; newly permitted retries at once.
void testRetryBackoff() {
    Shell s;
    s.refuseStart = true;
    for (int64_t t = 0; t < src::kRetryMs; t += kPollMs) s.poll(t, false, 0, true);
    CHECK(s.starts == 1, "refused start retried %d times inside the back-off", s.starts);
    s.poll(src::kRetryMs, false, 0, true);
    CHECK(s.starts == 2, "no retry after the back-off (%d)", s.starts);
    s.poll(src::kRetryMs + kPollMs, false, 0, false);  // permission off
    s.refuseStart = false;
    s.poll(src::kRetryMs + 2 * kPollMs, false, 0, true);  // just granted
    CHECK(s.starts == 3 && s.usingCapture, "a fresh grant did not retry at once (%d)", s.starts);

    // A stop by the shell (hidden wallpaper) is not a refusal: the next poll starts again at once.
    s.captureRunning = false;
    s.poll(src::kRetryMs + 3 * kPollMs, false, 0, true);
    CHECK(s.starts == 4 && s.usingCapture, "restart after a shell stop waited (%d)", s.starts);
}

// A new ring (another session) is tracked from scratch, with the first-sight grace.
void testNewRingGrace() {
    Shell s;
    s.poll(0, true, 1000, true);
    CHECK(!s.usingCapture && s.starts == 0, "first sight of a ring opened the capture");
}

// The C ABI carries the same rule through a zeroed long array.
void testAbi() {
    long long state[GEODE_SOUND_SOURCE_STATE_LONGS] = {};
    int out = geode_sound_source_step(state, 0, 0, 0, 0, 1, 0);
    CHECK(out == (GEODE_SOUND_SOURCE_USE_CAPTURE | GEODE_SOUND_SOURCE_START_CAPTURE), "abi first answer %d", out);
    geode_sound_source_start_failed(state, 0);
    out = geode_sound_source_step(state, kPollMs, 0, 0, 0, 1, 0);
    CHECK(out == GEODE_SOUND_SOURCE_USE_CAPTURE, "abi retried inside the back-off: %d", out);
    out = geode_sound_source_step(state, 2 * kPollMs, 0, 0, 0, 0, 1);
    CHECK(out == GEODE_SOUND_SOURCE_STOP_CAPTURE, "abi did not stop a forbidden capture: %d", out);
    CHECK(geode_sound_source_step(nullptr, 0, 0, 0, 0, 1, 0) == 0, "null state answered");
}

}  // namespace

int main() {
    testFedRingWins();
    testStallFallsBack();
    testNoRingUsesCapture();
    testNoFlapAtBoundary();
    testNoPermissionStaysOnRing();
    testRetryBackoff();
    testNewRingGrace();
    testAbi();
    if (failures) {
        std::fprintf(stderr, "sound_source_test: %d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("sound_source_test: ok\n");
    return EXIT_SUCCESS;
}
