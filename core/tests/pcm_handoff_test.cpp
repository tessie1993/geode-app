// Host-side checks for the raw-PCM hand-off (core/viz/PcmHandoff). Built and run by CI ("Native host
// tests" in .github/workflows/android.yml) with the system compiler; no GL, no device.
//
// What is pinned: a pushed block is delivered once, so a frame with no new push (paused, starved) hands
// the scenes nothing; and a PcmPulse fed nothing decays to 0 instead of holding its last peak.
#include <cstdio>
#include <vector>

#include "viz/PcmHandoff.hpp"

using geode::viz::PcmHandoff;
using geode::viz::PcmPulse;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

void blockIsDeliveredOnce() {
    PcmHandoff handoff(8);
    std::vector<float> out(8, 0.0f);
    check(handoff.take(out.data()) == 0, "nothing pushed, nothing taken");
    const float block[] = {0.1f, -0.9f, 0.3f};
    handoff.push(block, 3);
    check(handoff.take(out.data()) == 3, "the pushed block is taken");
    check(out[1] == -0.9f, "with its samples");
    check(handoff.take(out.data()) == 0, "a second frame with no new push takes nothing");
    handoff.push(block, 3);
    check(handoff.take(out.data()) == 3, "a new push is taken again");
}

void keepsTheNewestSamples() {
    PcmHandoff handoff(4);
    std::vector<float> out(4, 0.0f);
    const float block[] = {1, 2, 3, 4, 5, 6};
    handoff.push(block, 6);
    check(handoff.take(out.data()) == 4, "a long block is cut to capacity");
    check(out[0] == 3.0f && out[3] == 6.0f, "keeping its newest samples");
    handoff.push(nullptr, 3);
    handoff.push(block, 0);
    check(handoff.take(out.data()) == 0, "empty pushes leave nothing pending");
}

void pulseDecaysWithoutNewPcm() {
    PcmHandoff handoff(8);
    PcmPulse pulse;
    std::vector<float> out(8, 0.0f);
    const float loud[] = {0.8f, -1.0f, 0.5f};
    handoff.push(loud, 3);
    // Sixty frames as Renderer runs them: take once per frame, deliver only what was taken.
    float first = 0.0f;
    float last = 1.0f;
    for (int frame = 0; frame < 60; ++frame) {
        const int n = handoff.take(out.data());
        if (n > 0) pulse.accept(out.data(), n);
        const float level = pulse.tick(1.0f / 60.0f);
        if (frame == 0) first = level;
        last = level;
    }
    check(first == 1.0f, "the pulse peaks on the pushed block");
    check(last == 0.0f, "with no new PCM it decays to 0 within a second");
}

}  // namespace

int main() {
    blockIsDeliveredOnce();
    keepsTheNewestSamples();
    pulseDecaysWithoutNewPcm();
    if (failures == 0) std::printf("pcm_handoff_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
