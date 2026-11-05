// Host-side checks that envelopes (ADSR) and LFOs on luminance targets reach the flash limiter (#363).
// Built and run by CI ("Native host tests" in .github/workflows/android.yml); no GL, no device.
//
// The issue's failure scenario, on the engine's own code: Envelope 1 on Brightness, attack 5 ms, decay
// 10 ms, sustain 0, amount 150 %, retrigger, hi-hats at 8 onsets a second; and an LFO slot, source
// Transient, target Brightness, depth 2. Before #363 nothing fed their swings to any rate limit. Here the
// limiter gets the drive alone (no measured frame lands at all, the worst case of the readback's lag), and
// a WCAG-style oracle counts the swings that are still shown through the grade.
//
// What is pinned: both strobes engage the hold within the first quarter second and show at most three
// flashes in any second; a slow envelope, a slow LFO and an envelope on a non-luminance target pass
// untouched (scale exactly 1); the hold lets go once the hi-hats stop.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <functional>

#include "viz/Adsr.hpp"
#include "viz/FlashLimiter.hpp"
#include "viz/Lfo.hpp"

using namespace geode::viz;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

constexpr double kFps = 60.0;

struct Run {
    float firstHeldAt = -1.0f;   // seconds; -1 never held
    float minScale = 1.0f;
    int mostShownTransitions = 0;  // the oracle's most opposing swings in any second of what was shown
    bool heldAtEnd = false;
    float scaleAtEnd = 1.0f;
};

// The oracle, apart from the limiter's counter: swings of at least kDriveThreshold in the modulation as
// the grade lets it through (drive × scale: held, the picture moves kHeldScale of what the source moves).
struct Oracle {
    std::deque<double> times;
    float lo = 0.0f, hi = 0.0f;
    int dir = 0;
    bool started = false;
    int most = 0;
    void step(float v, double t) {
        if (!started) {
            started = true;
            lo = hi = v;
            return;
        }
        if (dir >= 0) hi = std::max(hi, v);
        if (dir <= 0) lo = std::min(lo, v);
        const float th = safety::FlashLimiter::kDriveThreshold;
        if (dir <= 0 && v - lo >= th) {
            dir = 1;
            hi = v;
            times.push_back(t);
        } else if (dir >= 0 && hi - v >= th) {
            dir = -1;
            lo = v;
            times.push_back(t);
        }
        while (!times.empty() && times.front() <= t - 1.0) times.pop_front();
        most = std::max(most, static_cast<int>(times.size()));
    }
};

// `onsetsHz` hi-hats for `strobeSeconds`, then silence until `seconds`. The engines are stepped as
// RendererFrame steps them: ADSR, its chain offsets, the LFOs, then the drive into the limiter.
Run simulate(const AdsrEngine& adsrIn, const LfoEngine& lfoIn, double onsetsHz, double strobeSeconds, double seconds) {
    AdsrEngine adsr = adsrIn;
    LfoEngine lfo = lfoIn;
    safety::FlashLimiter limiter;
    Oracle oracle;
    Run run;
    const float dt = static_cast<float>(1.0 / kFps);
    const int frames = static_cast<int>(seconds * kFps);
    std::array<float, LfoEngine::kSlots> envRate{};
    std::array<float, LfoEngine::kSlots> envDepth{};
    double nextOnset = 0.05;
    float onsetEnv = 0.0f;
    for (int f = 1; f <= frames; f++) {
        const double t = f / kFps;
        GeodeSoundFrame sound{};
        sound.level = 0.5f;
        sound.trebleAtt = 1.0f;
        if (onsetsHz > 0.0 && t < strobeSeconds && t >= nextOnset) {
            sound.onset = 1.0f;
            onsetEnv = 1.0f;
            nextOnset += 1.0 / onsetsHz;
        } else {
            onsetEnv *= std::exp(-dt / 0.03f);
        }
        sound.onsetEnv = onsetEnv;
        const auto& envs = adsr.tick(dt, sound);
        AdsrEngine::lfoOffsets(adsr.configs, envs, envRate, envDepth);
        const auto& values = lfo.tick(dt, sound, envRate.data(), envDepth.data());
        LuminanceDrive drive{};
        lfo.addLuminanceDrive(values, drive);
        AdsrEngine::addLuminanceDrive(adsr.configs, envs, drive);
        limiter.drive(drive, t);
        const safety::Grade g = limiter.next(t);
        if (limiter.held() && run.firstHeldAt < 0.0f) run.firstHeldAt = static_cast<float>(t);
        run.minScale = std::min(run.minScale, g.scale);
        float shown = 0.0f;  // each scenario drives one target, so the sum is that target's drive
        for (const float d : drive) shown += d;
        oracle.step(shown * g.scale, t);
        run.heldAtEnd = limiter.held();
        run.scaleAtEnd = g.scale;
    }
    run.mostShownTransitions = oracle.most;
    return run;
}

AdsrEngine envelope(LfoTarget target, float attack, float decay, float sustain, float amount) {
    AdsrEngine e;
    AdsrConfig& c = e.configs[0];
    c.enabled = true;
    c.targets = {target};
    c.attack = attack;
    c.decay = decay;
    c.sustain = sustain;
    c.release = 0.05f;
    c.amount = amount;
    c.band = EnvBand::Treble;
    c.gateThreshold = 0.25f;
    c.retrigger = true;
    return e;
}

LfoEngine lfoSlot(ModSource source, LfoTarget target, float depth, float rateSeconds = 2.0f) {
    LfoEngine l;
    LfoConfig& c = l.configs[0];
    c.enabled = true;
    c.source = source;
    c.target = target;
    c.depth = depth;
    c.rateSeconds = rateSeconds;
    c.wave = LfoWave::Sine;
    return l;
}

void strobeEngagesAndHolds(const char* name, const AdsrEngine& adsr, const LfoEngine& lfo) {
    const Run r = simulate(adsr, lfo, 8.0, 3.0, 3.0);
    std::printf("%s: held at %.3f s, min scale %.3f, most shown transitions/s %d\n", name, r.firstHeldAt, r.minScale,
                r.mostShownTransitions);
    check(r.firstHeldAt > 0.0f && r.firstHeldAt <= 0.25f, "an 8 Hz luminance strobe engages the hold within 0.25 s");
    check(r.minScale <= safety::FlashLimiter::kHeldScale + 1.0e-6f, "held, the strobe is squeezed to the held scale");
    check(r.mostShownTransitions <= 6, "at most three flashes in any second reach the screen");
    check(r.heldAtEnd, "the hold lasts while the strobe does");
}

void untouched(const char* name, const AdsrEngine& adsr, const LfoEngine& lfo, double onsetsHz) {
    const Run r = simulate(adsr, lfo, onsetsHz, 4.0, 4.0);
    std::printf("%s: held at %.3f s, min scale %.3f\n", name, r.firstHeldAt, r.minScale);
    check(r.firstHeldAt < 0.0f && r.minScale == 1.0f, name);
}

}  // namespace

int main() {
    const LfoEngine noLfo;
    const AdsrEngine noEnvelope;

    // The issue's two scenarios.
    strobeEngagesAndHolds("ADSR 5 ms / 10 ms x150% on Brightness",
                          envelope(LfoTarget::Brightness, 0.005f, 0.01f, 0.0f, 1.5f), noLfo);
    strobeEngagesAndHolds("LFO Transient follower x2 on Brightness", noEnvelope,
                          lfoSlot(ModSource::Transient, LfoTarget::Brightness, 2.0f));
    // An envelope steering an LFO's depth reaches the limiter through that LFO.
    {
        // LFO 1 hears a steady level (Positive: 0.5) at depth 0, so only the envelope's depth moves it.
        LfoEngine l = lfoSlot(ModSource::Level, LfoTarget::Intensity, 0.0f);
        l.configs[0].polarity = ModPolarity::Positive;
        strobeEngagesAndHolds("ADSR on LFO 1 depth, LFO 1 Level on Intensity",
                              envelope(LfoTarget::Lfo1Depth, 0.005f, 0.01f, 0.0f, 1.5f), l);
    }

    // What must pass untouched.
    untouched("a slow envelope on Brightness (1 onset/s, amount 20 %) is untouched",
              envelope(LfoTarget::Brightness, 0.08f, 0.35f, 0.5f, 0.2f), noLfo, 1.0);
    untouched("an 8 Hz envelope on Speed (not a luminance target) is untouched",
              envelope(LfoTarget::Speed, 0.005f, 0.01f, 0.0f, 1.5f), noLfo, 8.0);
    untouched("a 0.25 Hz sine LFO on Brightness is untouched", noEnvelope,
              lfoSlot(ModSource::Lfo, LfoTarget::Brightness, 1.0f, 4.0f), 0.0);

    // Once the hi-hats stop, the hold lets go and the picture comes back whole.
    {
        const Run r = simulate(envelope(LfoTarget::Brightness, 0.005f, 0.01f, 0.0f, 1.5f), noLfo, 8.0, 2.0, 5.0);
        std::printf("release: held at end %d, scale at end %.3f\n", r.heldAtEnd ? 1 : 0, r.scaleAtEnd);
        check(!r.heldAtEnd && r.scaleAtEnd == 1.0f, "the hold releases fully once the strobe stops");
    }

    if (failures == 0) std::printf("luminance_drive_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
