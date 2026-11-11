// Host test of the note & key reactions (viz/NoteResponse): that the six style knobs each answer to at
// least two fields of the sound frame, that a percussive track raises the drum weight and a tonal one
// lowers it, that the knobs are all it hands on (no scene param, no colour), that it is deterministic,
// that off is exactly 0.5 on every knob, that silence is stable, and that the user's amounts scale each
// source.
// Built by CI (.github/workflows/android.yml, "Note response test") with core/viz/NoteResponse.cpp alone:
// the emotion state is an input, so the test sets it directly.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <string>

#include "viz/NoteResponse.hpp"

using geode::sound::Emotion;
using geode::viz::NoteResponse;
using geode::viz::NoteResponseConfig;
using geode::viz::NoteUniforms;
using geode::viz::SceneParams;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL %s\n", what.c_str());
        ++failures;
    }
}

constexpr float kDt = 1.0f / 60.0f;

// A steady, mid-everything frame: music playing, a key, no events.
GeodeSoundFrame base() {
    GeodeSoundFrame f{};
    f.bass = f.mid = f.treble = f.vol = 1.0f;
    f.bassAtt = f.midAtt = f.trebleAtt = f.volAtt = 1.0f;
    f.level = 0.5f;
    f.intensity = 0.5f;
    f.drive = 1.0f;  // full reach: the knobs read at the depth these tests say
    f.loudness = 0.7f;
    f.centroid = 0.5f;
    f.densityFast = 0.5f;
    f.densitySlow = 0.5f;
    f.flatness = 0.2f;
    f.energyTrend = 0.5f;
    f.peakFreq = 0.5f;
    f.stereoWidth = 0.2f;
    f.hfc = 1.0f;
    f.flux = 1.0f;
    f.keyRoot = 0.0f;
    f.keyHue = 0.0f;
    f.keyMajor = 1.0f;
    f.keyClarity = 0.6f;
    f.scaleFit = 0.6f;
    f.tension = 0.2f;
    f.pitch = 60.0f;
    f.pitchClass = 0.0f;
    f.pitchHeight = 0.5f;
    f.pitchConfidence = 0.7f;
    return f;
}

Emotion::State calm() {
    Emotion::State e;
    e.valence = 0.3f;
    e.energy = 0.5f;
    e.tension = 0.3f;
    return e;
}

NoteResponse on(uint64_t seed = 7) {
    NoteResponse n;
    n.configure({true, seed});
    return n;
}

// Run `seconds` of frames made by `make(t)`.
void run(NoteResponse& n, float seconds, const std::function<GeodeSoundFrame(float)>& make, const Emotion::State& e = calm(),
         NoteResponse::Inputs in = {}) {
    const int frames = static_cast<int>(seconds / kDt);
    for (int i = 0; i < frames; ++i) n.step(make(i * kDt), e, in, kDt);
}

// The knob `k` after four seconds of a steady frame, with `tweak` applied to it.
float knobWith(int k, const std::function<void(GeodeSoundFrame&, Emotion::State&)>& tweak) {
    NoteResponse n = on();
    Emotion::State e = calm();
    GeodeSoundFrame f = base();
    tweak(f, e);
    run(n, 4.0f, [&](float) { return f; }, e);
    return n.uniforms().knobs[k];
}

void knobsAnswerSeveralFields() {
    const auto none = [](GeodeSoundFrame&, Emotion::State&) {};
    struct Probe {
        int knob;
        const char* field;
        std::function<void(GeodeSoundFrame&, Emotion::State&)> tweak;
    };
    // Every knob is pushed by at least two independent fields.
    const Probe probes[] = {
        {0, "kickEnv", [](GeodeSoundFrame& f, Emotion::State&) { f.kickEnv = 1.0f; }},
        {0, "onsetEnv", [](GeodeSoundFrame& f, Emotion::State&) { f.onsetEnv = 1.0f; }},
        {0, "bassAtt", [](GeodeSoundFrame& f, Emotion::State&) { f.bassAtt = 2.0f; }},
        {1, "centroid", [](GeodeSoundFrame& f, Emotion::State&) { f.centroid = 0.85f; }},
        {1, "densityFast", [](GeodeSoundFrame& f, Emotion::State&) { f.densityFast = 1.0f; }},
        {1, "flatness", [](GeodeSoundFrame& f, Emotion::State&) { f.flatness = 0.7f; }},
        {2, "intensity", [](GeodeSoundFrame& f, Emotion::State&) { f.intensity = 1.0f; }},
        {2, "level", [](GeodeSoundFrame& f, Emotion::State&) { f.level = 1.0f; }},
        {2, "energyTrend", [](GeodeSoundFrame& f, Emotion::State&) { f.energyTrend = 1.0f; }},
        {3, "pitchHeight", [](GeodeSoundFrame& f, Emotion::State&) { f.pitchHeight = 0.95f; }},
        {3, "peakFreq", [](GeodeSoundFrame& f, Emotion::State&) { f.peakFreq = 0.95f; }},
        {4, "emotion tension", [](GeodeSoundFrame&, Emotion::State& e) { e.tension = 0.9f; }},
        {4, "buildUp", [](GeodeSoundFrame& f, Emotion::State&) { f.buildUp = 1.0f; }},
        {4, "harmonic tension", [](GeodeSoundFrame& f, Emotion::State&) { f.tension = 0.9f; }},
        {5, "stereoWidth", [](GeodeSoundFrame& f, Emotion::State&) { f.stereoWidth = 0.8f; }},
    };
    int perKnob[6] = {};
    for (const Probe& p : probes) {
        const float rest = knobWith(p.knob, none);
        const float pushed = knobWith(p.knob, p.tweak);
        const bool moved = std::fabs(pushed - rest) > 0.03f;
        check(moved, "knob k" + std::to_string(p.knob) + " answers " + p.field);
        perKnob[p.knob] += moved ? 1 : 0;
    }
    // k5 SPACE also hears interval motion: a run of leaps opens it.
    {
        NoteResponse still = on();
        run(still, 4.0f, [](float) { return base(); });
        NoteResponse leaping = on();
        run(leaping, 4.0f, [](float t) {
            GeodeSoundFrame f = base();
            const int step = static_cast<int>(t / 0.5f);
            if (std::fmod(t, 0.5f) < kDt) {
                f.noteChange = 0.8f;
                f.interval = (step % 2 == 0) ? 0.75f : -0.75f;
            }
            return f;
        });
        const bool moved = leaping.uniforms().knobs[5] > still.uniforms().knobs[5] + 0.03f;
        check(moved, "knob k5 answers intervals");
        perKnob[5] += moved ? 1 : 0;
    }
    for (int k = 0; k < 6; ++k) check(perKnob[k] >= 2, "knob k" + std::to_string(k) + " answers at least two fields");
}

GeodeSoundFrame percussive(float t) {
    GeodeSoundFrame f = base();
    f.flatness = 0.6f;
    f.pitchConfidence = 0.1f;
    f.keyClarity = 0.1f;
    f.scaleFit = 0.15f;
    f.hfc = 2.5f;
    const float beat = std::fmod(t, 0.25f);
    if (beat < kDt) {
        f.kick = 1.0f;
        f.onset = 1.0f;
        f.kickEnv = 1.0f;
    }
    if (std::fmod(t + 0.125f, 0.25f) < kDt) f.hat = 0.6f;
    f.kickEnv = std::exp(-beat / 0.15f);
    return f;
}

GeodeSoundFrame tonal(float t) {
    GeodeSoundFrame f = base();
    f.flatness = 0.05f;
    f.pitchConfidence = 0.85f;
    f.keyClarity = 0.85f;
    f.scaleFit = 0.85f;
    // A soft onset every second, a kick-like envelope now and then.
    const float beat = std::fmod(t, 1.0f);
    if (beat < kDt) f.onset = 1.0f;
    f.kickEnv = std::exp(-beat / 0.15f);
    return f;
}

void drumWeightFollowsTheInput() {
    NoteResponse drums = on();
    run(drums, 12.0f, percussive);
    NoteResponse tones = on();
    run(tones, 12.0f, tonal);
    std::printf("percussiveness: drums %.2f, tones %.2f\n", drums.percussiveness(), tones.percussiveness());
    check(drums.percussiveness() > 0.7f, "a drum track reads percussive");
    check(tones.percussiveness() < 0.3f, "a tonal track reads tonal");
    // The same kick envelope moves PULSE far more on the drum track.
    float drumPeak = 0.0f, tonePeak = 0.0f;
    for (int i = 0; i < 120; ++i) {
        const float t = 12.0f + i * kDt;
        drums.step(percussive(t), calm(), {}, kDt);
        tones.step(tonal(t), calm(), {}, kDt);
        drumPeak = std::max(drumPeak, drums.uniforms().knobs[0]);
        tonePeak = std::max(tonePeak, tones.uniforms().knobs[0]);
    }
    check(drumPeak > tonePeak + 0.2f, "PULSE weighs the drums by how percussive the track is");
    // Continuous: switching from drums to tones moves the weight down over seconds, not at once.
    run(drums, 1.0f, tonal);
    const float after1 = drums.percussiveness();
    run(drums, 10.0f, tonal);
    check(after1 > drums.percussiveness() + 0.1f && after1 < 0.95f, "the drum weight glides between tracks");
}

// A melody: the C major scale up and down, one note every 0.4 s.
GeodeSoundFrame melody(float t) {
    static const int kScale[] = {0, 2, 4, 5, 7, 9, 11, 12, 11, 9, 7, 5, 4, 2};
    GeodeSoundFrame f = base();
    const int i = static_cast<int>(t / 0.4f);
    const int note = kScale[i % 14];
    const int prev = kScale[(i + 13) % 14];
    f.pitch = 60.0f + note;
    f.pitchClass = static_cast<float>(note % 12) / 12.0f;
    f.pitchHeight = (f.pitch - 36.0f) / 48.0f;
    if (std::fmod(t, 0.4f) < kDt && i > 0) {
        f.noteChange = 0.8f;
        f.interval = static_cast<float>(note - prev) / 12.0f;
        f.onsetEnv = 0.8f;
    }
    return f;
}

// The owner's rules, "no hardcoded reactions" and "no colour narrowing": the layer used to lay a twist on
// every leap, a bend on tension, a fold shift on every chord change, a zoom on the drums and a tint toward
// the notes' colour over every style alike. It now hands on the six knobs and its amount, nothing else:
// no call takes or returns scene params, and the uniforms hold no colour.
template <typename N>
concept WritesParams = requires(const N& n, const SceneParams& p) { n.apply(p); };
static_assert(!WritesParams<NoteResponse>, "the note layer writes no scene param");
static_assert(sizeof(NoteUniforms) == (NoteUniforms::kKnobs + 1) * sizeof(float), "the note uniforms are the six knobs and the amount: no tint, no hues");

void deterministic() {
    const int frames = static_cast<int>(8.0f / kDt);
    NoteResponse n = on();
    NoteResponse again = on();
    for (int i = 0; i < frames; ++i) {
        n.step(melody(i * kDt), calm(), {}, kDt);
        again.step(melody(i * kDt), calm(), {}, kDt);
    }
    check(std::memcmp(&again.uniforms(), &n.uniforms(), sizeof(NoteUniforms)) == 0, "same seed and frames: same uniforms");
}

void chordChangesSwing() {
    NoteResponse n = on();
    float peak = 0.0f;
    const int frames = static_cast<int>(10.0f / kDt);
    for (int i = 0; i < frames; ++i) {
        GeodeSoundFrame f = base();
        if (std::fmod(i * kDt, 2.0f) < kDt && i * kDt > 0.5f) f.harmonicChange = 0.6f;
        n.step(f, calm(), {}, kDt);
        peak = std::max(peak, std::fabs(n.swing()));
    }
    check(n.chordShifts() >= 4 && peak > 0.05f, "chord changes swing the SPACE knob's spring");
    // Chatter: harmonicChange every frame still counts at most once per 1.5 s.
    NoteResponse chatter = on();
    run(chatter, 3.0f, [](float) {
        GeodeSoundFrame f = base();
        f.harmonicChange = 0.6f;
        return f;
    });
    check(chatter.chordShifts() <= 2, "chord-change chatter is rate limited");
}

void intervalsMove() {
    NoteResponse leap = on();
    GeodeSoundFrame f = base();
    f.noteChange = 0.8f;
    f.interval = 0.75f;
    leap.step(f, calm(), {}, kDt);
    run(leap, 0.3f, [](float) { return base(); });
    check(std::fabs(leap.swing()) > 0.2f, "a leap swings");
    NoteResponse step = on();
    f.interval = 2.0f / 12.0f;
    step.step(f, calm(), {}, kDt);
    run(step, 0.3f, [](float) { return base(); });
    check(std::fabs(step.swing()) < 0.05f && step.glide() > 0.1f, "a step glides without swinging");
}

void offIsToday() {
    NoteResponse n;
    n.configure({false, 7});
    run(n, 6.0f, melody);
    const NoteUniforms& u = n.uniforms();
    for (float k : u.knobs) check(k == 0.5f, "off: every knob at exactly 0.5");
    check(u.amount == 0.0f, "off: no amount");
    // Switched off mid-song: eases out, then is exactly off again.
    NoteResponse fading = on();
    run(fading, 4.0f, melody);
    fading.configure({false, 7});
    run(fading, 0.2f, melody);
    check(fading.amount() > 0.0f && fading.amount() < 1.0f, "switching off eases out");
    run(fading, 8.0f, melody);
    check(fading.amount() == 0.0f, "then it is off");
    for (float k : fading.uniforms().knobs) check(k == 0.5f, "and every knob is back at 0.5");
}

void silenceIsStable() {
    NoteResponse n = on();
    run(n, 20.0f, [](float) { return GeodeSoundFrame{}; }, Emotion::State{});
    const NoteUniforms& u = n.uniforms();
    for (float k : u.knobs) check(std::isfinite(k) && k >= 0.0f && k <= 1.0f, "silence: knobs finite and in range");
    check(std::isfinite(n.percussiveness()) && std::fabs(n.percussiveness() - 0.5f) < 0.2f, "silence: drum weight undecided");
    check(std::fabs(n.swing()) < 1e-3f && std::fabs(n.glide()) < 1e-3f, "silence: still");
    // A garbage frame does not poison it.
    GeodeSoundFrame bad{};
    bad.interval = NAN;
    bad.noteChange = 1.0f;
    bad.pitch = 60.0f;
    bad.pitchConfidence = 0.9f;
    bad.tension = NAN;
    bad.hfc = NAN;
    bad.keyHue = NAN;
    bad.flatness = NAN;
    n.step(bad, Emotion::State{}, {NAN, NAN, NAN, NAN, NAN}, kDt);
    run(n, 1.0f, [](float) { return GeodeSoundFrame{}; }, Emotion::State{});
    for (float k : n.uniforms().knobs) check(std::isfinite(k), "a NaN frame leaves the knobs finite");
    check(std::isfinite(n.swing()) && std::isfinite(n.glide()) && std::isfinite(n.percussiveness()), "a NaN frame leaves the motion finite");
}

void newPlayForgets() {
    NoteResponse n = on(1);
    run(n, 5.0f, [](float t) {
        GeodeSoundFrame f = base();
        if (std::fmod(t, 2.0f) < kDt && t > 0.5f) f.harmonicChange = 0.6f;
        return f;
    });
    check(n.chordShifts() > 0, "chord changes before the new play");
    n.configure({true, 2});
    check(n.chordShifts() == 0, "a new seed is a new play");
}

// A minor-key line with leaps and a tritone, rising tension and a chord change every two seconds.
GeodeSoundFrame minorLeaps(float t) {
    static const int kLine[] = {9, 16, 12, 18, 21, 15, 9, 24, 17, 11};
    GeodeSoundFrame f = base();
    f.keyMajor = 0.0f;
    f.keyRoot = 9.0f / 12.0f;
    f.keyHue = 0.25f;
    f.tension = std::min(1.0f, 0.3f + 0.08f * t);
    const int i = static_cast<int>(t / 0.5f);
    const int note = kLine[i % 10];
    const int prev = kLine[(i + 9) % 10];
    f.pitch = 48.0f + note;
    f.pitchClass = static_cast<float>(note % 12) / 12.0f;
    f.pitchHeight = (f.pitch - 36.0f) / 48.0f;
    if (std::fmod(t, 0.5f) < kDt && i > 0) {
        f.noteChange = 0.9f;
        f.interval = static_cast<float>(note - prev) / 12.0f;
        f.onsetEnv = 0.9f;
    }
    if (std::fmod(t, 2.0f) < kDt && t > 0.5f) f.harmonicChange = 0.7f;
    return f;
}

// The style's Notes & key amounts scale what feeds each knob, down to nothing at 0.
void amountsScaleTheKnobs() {
    NoteResponse::Inputs quiet;
    quiet.chords = 0.0f;
    quiet.intervals = 0.0f;
    NoteResponse muted = on();
    run(muted, 6.0f, minorLeaps, calm(), quiet);
    check(muted.chordShifts() == 0, "Chord changes 0 never counts a chord");
    check(std::fabs(muted.swing()) < 1e-3f && std::fabs(muted.glide()) < 1e-3f, "Leaps and steps 0 never swings or glides");

    const auto knobAt = [](int k, const NoteResponse::Inputs& in, const std::function<void(GeodeSoundFrame&, Emotion::State&)>& tweak) {
        NoteResponse n = on();
        Emotion::State e = calm();
        GeodeSoundFrame f = base();
        tweak(f, e);
        run(n, 4.0f, [&](float) { return f; }, e, in);
        return n.uniforms().knobs[k];
    };
    const auto high = [](GeodeSoundFrame& f, Emotion::State&) { f.pitchHeight = 0.95f; };
    const auto low = [](GeodeSoundFrame& f, Emotion::State&) { f.pitchHeight = 0.05f; };
    NoteResponse::Inputs noPitch;
    noPitch.pitch = 0.0f;
    check(knobAt(3, {}, high) > knobAt(3, {}, low) + 0.1f, "Pitch height 1: TONE follows the melody");
    check(knobAt(3, noPitch, high) == knobAt(3, noPitch, low), "Pitch height 0: TONE holds still");
    const auto tense = [](GeodeSoundFrame& f, Emotion::State& e) { f.tension = e.tension = 0.9f; };
    const auto rest = [](GeodeSoundFrame& f, Emotion::State& e) { f.tension = e.tension = 0.0f; };
    NoteResponse::Inputs noTension;
    noTension.tension = 0.0f;
    check(knobAt(4, {}, tense) > knobAt(4, {}, rest) + 0.1f, "Tension 1: BUILD follows the tension");
    check(knobAt(4, noTension, tense) == knobAt(4, noTension, rest), "Tension 0: BUILD ignores it");
    NoteResponse::Inputs noDrums;
    noDrums.drums = 0.0f;
    const auto hit = [](GeodeSoundFrame& f, Emotion::State&) { f.kickEnv = 1.0f; };
    const auto none = [](GeodeSoundFrame&, Emotion::State&) {};
    check(knobAt(0, {}, hit) > knobAt(0, {}, none) + 0.1f, "Drums 1: PULSE follows the drums");
    check(knobAt(0, noDrums, hit) == knobAt(0, noDrums, none), "Drums 0: PULSE ignores them");
}

}  // namespace

int main() {
    knobsAnswerSeveralFields();
    drumWeightFollowsTheInput();
    deterministic();
    chordChangesSwing();
    intervalsMove();
    offIsToday();
    silenceIsStable();
    newPlayForgets();
    amountsScaleTheKnobs();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("note response: all checks passed\n");
    return 0;
}
