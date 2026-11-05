// Host-side checks for the offline (export) frame grid (core/viz/frame/OfflineStep). Built and run by CI
// ("Native host tests" in .github/workflows/android.yml) with the system compiler; no GL, no device.
//
// What is pinned (#573): frame N's render time and its audio cut come from N alone, so they match what the
// Kotlin shell computed before the port (`frame * 1000 / fps`, `nextMs * rate / 1000`), never drift over a
// long render, hand every track sample to exactly one frame, and run on a broadcast rate's true grid.
#include <cstdint>
#include <cstdio>

#include "viz/frame/OfflineStep.hpp"

using geode::viz::frame::audioUntil;
using geode::viz::frame::frameMs;
using geode::viz::frame::frameSeconds;
using geode::viz::frame::OfflineRate;
using geode::viz::frame::pinOfflineParams;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

// The pre-port Kotlin, verbatim in integer maths: the reference the native grid must reproduce.
int64_t kotlinMs(int64_t frame, int64_t fps) { return frame * 1000 / fps; }
int64_t kotlinCut(int64_t frame, int64_t fps, int64_t rate) { return kotlinMs(frame + 1, fps) * rate / 1000; }

void matchesTheKotlinItReplaces() {
    const int64_t fpsList[] = {24, 25, 30, 50, 60, 120};
    const int64_t rates[] = {44100, 48000, 96000, 192000};
    bool same = true;
    for (int64_t fps : fpsList) {
        for (int64_t frame = 0; frame < 20000; ++frame) {
            if (frameMs(frame, {fps, 1}) != kotlinMs(frame, fps)) same = false;
            for (int64_t rate : rates) {
                if (audioUntil(frame, {fps, 1}, rate) != kotlinCut(frame, fps, rate)) same = false;
            }
        }
    }
    check(same, "integer fps: frame ms and audio cut equal the Kotlin they replace");
    check(frameMs(1, {60, 1}) == 16 && frameMs(2, {60, 1}) == 33 && frameMs(3, {60, 1}) == 50, "60 fps grid floors to ms");
    check(frameSeconds(3, {60, 1}) == 0.05, "seconds are the ms grid over 1000");
    check(audioUntil(0, {60, 1}, 48000) == 16 * 48, "frame 0 hears up to frame 1's start");
}

void noDriftOverThreeHours() {
    const OfflineRate fps{60, 1};
    const int64_t rate = 48000;
    const int64_t frames = 3 * 3600 * 60;  // 648 000 frames
    check(frameMs(frames, fps) == 3 * 3600 * 1000LL, "3 h at 60 fps ends on exactly 10 800 000 ms");
    check(audioUntil(frames - 1, fps, rate) == 3 * 3600 * rate, "the last frame's cut is exactly 3 h of samples");

    // Every sample goes to exactly one frame: cuts never step back, and the per-frame deliveries add up
    // to the total with nothing lost or heard twice. Each cut sits within one ms of the ideal instant.
    int64_t prev = 0;
    int64_t sum = 0;
    bool monotonic = true;
    bool near = true;
    bool steadyDt = true;
    for (int64_t f = 0; f < frames; ++f) {
        const int64_t cut = audioUntil(f, fps, rate);
        if (cut < prev) monotonic = false;
        sum += cut - prev;
        prev = cut;
        const int64_t ideal = (f + 1) * rate / 60;
        if (cut > ideal || ideal - cut > rate / 1000) near = false;
        const int64_t dt = frameMs(f + 1, fps) - frameMs(f, fps);
        if (dt != 16 && dt != 17) steadyDt = false;
    }
    check(monotonic, "audio cuts never step back");
    check(sum == 3 * 3600 * rate, "per-frame deliveries sum to the whole range");
    check(near, "every cut is within 1 ms of its ideal instant");
    check(steadyDt, "the engine steps by 16 or 17 ms at 60 fps, never more");
}

void broadcastRates() {
    const OfflineRate ntsc{30000, 1001};  // 29.97
    check(frameMs(30000, ntsc) == 1001000, "30000 frames at 29.97 fps last exactly 1001 s");
    check(frameMs(1, ntsc) == 33 && frameMs(2, ntsc) == 66 && frameMs(3, ntsc) == 100, "29.97 grid floors to ms");
    // Three hours of 29.97: the time never drifts from frame * 1001 / 30 s by more than the ms floor.
    const int64_t frames = 3 * 3600 * 30000 / 1001;
    bool exact = true;
    for (int64_t f = 0; f <= frames; f += 997) {
        const int64_t idealTimes30000 = f * 1001 * 1000;  // ms * 30000
        const int64_t ms = frameMs(f, ntsc);
        if (ms * 30000 > idealTimes30000 || idealTimes30000 - ms * 30000 >= 30000) exact = false;
    }
    check(exact, "29.97 fps: every frame time is the exact instant floored to the ms over 3 h");
    check(audioUntil(29999, ntsc, 48000) == 1001LL * 48000, "29.97 fps: the cut after 30000 frames is 1001 s of samples");
    const OfflineRate p5994{60000, 1001};
    check(frameMs(60000, p5994) == 1001000, "59.94 fps lands on its grid too");
}

void badInputsAreSilent() {
    check(frameMs(10, {0, 1}) == 0 && frameMs(10, {30, 0}) == 0 && frameMs(10, {-30, 1}) == 0, "bad rate gives 0 ms");
    check(frameMs(-5, {30, 1}) == 0, "negative frame gives 0 ms");
    check(audioUntil(10, {30, 1}, 0) == 0, "unknown sample rate hears nothing");
    check(audioUntil(-1, {30, 1}, 48000) == 0, "negative frame hears nothing");
}

void pinsQuality() {
    struct FakeParams {
        bool fluidAutoQuality = true;
        float other = 0.25f;
    } p;
    pinOfflineParams(p);
    check(!p.fluidAutoQuality && p.other == 0.25f, "offline pin turns off adaptive fluid quality only");
}

}  // namespace

int main() {
    matchesTheKotlinItReplaces();
    noDriftOverThreeHours();
    broadcastRates();
    badInputsAreSilent();
    pinsQuality();
    if (failures != 0) {
        std::printf("offline_step_test: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("offline_step_test: ok\n");
    return 0;
}
