// Host test of the note & key reactions (viz/NoteResponse): that the six style knobs each answer to at
// least two fields of the sound frame, that a percussive track raises the drum weight and a tonal one
// lowers it, that blooms spawn on note onsets only, stay bounded and are deterministic, that a melody
// paints more than one hue, that off is exactly today's picture, and that silence is stable.
// Built by CI (.github/workflows/android.yml, "Note response test") with core/viz/NoteResponse.cpp alone:
// the emotion state is an input, so the test sets it directly.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <set>
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

void bloomsFollowNotes() {
    // No note onsets: no blooms, however loud the drums.
    NoteResponse drums = on();
    run(drums, 6.0f, percussive);
    check(drums.spawned() == 0, "drums alone spawn no blooms");
    check(drums.uniforms().bloomCount == 0, "drums alone draw no blooms");

    NoteResponse n = on();
    int maxLive = 0;
    const int frames = static_cast<int>(8.0f / kDt);
    int onsets = 0;
    for (int i = 0; i < frames; ++i) {
        const GeodeSoundFrame f = melody(i * kDt);
        if (f.noteChange > 0.0f) ++onsets;
        n.step(f, calm(), {}, kDt);
        maxLive = std::max(maxLive, n.uniforms().bloomCount);
        check(n.uniforms().bloomCount <= NoteUniforms::kBlooms, "blooms stay bounded");
        for (int k = 0; k < n.uniforms().bloomCount; ++k) {
            const auto& a = n.uniforms().bloomA[k];
            const auto& b = n.uniforms().bloomB[k];
            if (!(a[0] >= 0.0f && a[0] <= 1.0f && a[1] >= 0.0f && a[1] <= 1.0f && b[1] >= 0.0f && b[1] <= 1.0f)) {
                check(false, "a bloom stays on screen with an envelope in 0..1");
                break;
            }
        }
    }
    check(n.spawned() == onsets, "one bloom per note onset (" + std::to_string(n.spawned()) + " for " + std::to_string(onsets) + ")");
    check(maxLive >= 2, "blooms overlap: the last note fades while the new one grows");

    // Hues: a melody paints more than one colour.
    std::set<int> hueBins;
    for (float h : n.uniforms().hues) hueBins.insert(static_cast<int>(h * 12.0f) % 12);
    check(hueBins.size() >= 4, "a melody paints at least four hues (" + std::to_string(hueBins.size()) + ")");
    float minHue = 1.0f, maxHue = 0.0f;
    for (float h : n.uniforms().hues) {
        minHue = std::min(minHue, h);
        maxHue = std::max(maxHue, h);
    }
    check(maxHue - minHue > 0.2f, "the melody's hues spread across the wheel");

    // Higher notes sit higher and stretch up.
    const auto& blooms = n.blooms();
    for (const auto& b : blooms) {
        if (!b.alive) continue;
        check(b.stretch >= 0.8f && b.stretch <= 1.3f, "stretch in range");
    }

    // Deterministic: same seed, same frames, same blooms; another seed, other places.
    NoteResponse again = on();
    NoteResponse other = on(99);
    for (int i = 0; i < frames; ++i) {
        again.step(melody(i * kDt), calm(), {}, kDt);
        other.step(melody(i * kDt), calm(), {}, kDt);
    }
    check(std::memcmp(&again.uniforms(), &n.uniforms(), sizeof(NoteUniforms)) == 0, "same seed and frames: same blooms");
    bool differs = false;
    for (int k = 0; k < NoteUniforms::kBlooms; ++k) differs = differs || other.uniforms().bloomC[k][2] != n.uniforms().bloomC[k][2];
    check(differs, "another seed draws other blooms");
}

void chordChangesShiftTheScene() {
    NoteResponse n = on();
    run(n, 10.0f, [](float t) {
        GeodeSoundFrame f = base();
        if (std::fmod(t, 2.0f) < kDt && t > 0.5f) f.harmonicChange = 0.6f;
        return f;
    });
    check(n.sceneShifts() >= 4, "chord changes shift the scene");
    check(n.symmetryShift() != 0 && std::abs(n.symmetryShift()) <= 3, "the fold count moves, within three folds");
    SceneParams p;
    const SceneParams q = n.apply(p);
    check(q.symmetry == p.symmetry + n.symmetryShift(), "the shift reaches the fold count");
    // Chatter: harmonicChange every frame still shifts at most once per 1.5 s.
    NoteResponse chatter = on();
    run(chatter, 3.0f, [](float) {
        GeodeSoundFrame f = base();
        f.harmonicChange = 0.6f;
        return f;
    });
    check(chatter.sceneShifts() <= 2, "chord-change chatter is rate limited");
}

void tensionDistortsAndResolves() {
    NoteResponse n = on();
    Emotion::State tense = calm();
    tense.tension = 0.9f;
    run(n, 2.0f, [](float) {
        GeodeSoundFrame f = base();
        f.tension = 0.95f;
        return f;
    }, tense);
    const float strained = n.distortion();
    const SceneParams p;
    const SceneParams q = n.apply(p);
    check(strained > 0.6f, "tension bends the picture");
    check(q.warp > p.warp + 0.15f && q.chromaAb > p.chromaAb + 0.05f, "the bend reaches warp and chromatic split");
    run(n, 4.0f, [](float) {
        GeodeSoundFrame f = base();
        f.tension = 0.05f;
        return f;
    });
    check(n.distortion() < 0.1f, "resolution smooths it back");
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

void keyAndMood() {
    // Major warm, minor cool (with no director).
    NoteResponse major = on();
    run(major, 4.0f, [](float) { return base(); });
    NoteResponse minor = on();
    run(minor, 4.0f, [](float) {
        GeodeSoundFrame f = base();
        f.keyMajor = 0.0f;
        f.keyRoot = 9.0f / 12.0f;
        f.keyHue = 0.0f;
        return f;
    });
    const SceneParams p;
    check(major.apply(p).temperature > p.temperature + 0.05f, "a major key warms");
    check(minor.apply(p).temperature < p.temperature - 0.05f, "a minor key cools");
    auto distanceTo = [](float h, float target) { return std::fabs(std::fmod(h - target + 1.5f, 1.0f) - 0.5f); };
    check(distanceTo(major.anchorHue(), 0.07f) < distanceTo(minor.anchorHue(), 0.07f), "major sits nearer amber than minor");
    // Under the director its look's hue leads, and the layer leaves temperature to it.
    NoteResponse directed = on();
    run(directed, 6.0f, [](float) { return base(); }, calm(), {0.4f, 1.0f});
    check(distanceTo(directed.anchorHue(), 0.4f) < 0.02f, "the director's hue anchors the notes");
    check(std::fabs(directed.apply(p).temperature - p.temperature) < 1e-4f, "the director keeps the palette's temperature");
}

void offIsToday() {
    NoteResponse n;
    n.configure({false, 7});
    run(n, 6.0f, melody);
    const NoteUniforms& u = n.uniforms();
    for (float k : u.knobs) check(k == 0.5f, "off: every knob at exactly 0.5");
    check(u.bloomCount == 0 && n.spawned() == 0, "off: no blooms");
    SceneParams p;
    p.warp = 0.3f;
    p.symmetry = 5;
    const SceneParams q = n.apply(p);
    check(std::memcmp(&p, &q, sizeof(SceneParams)) == 0 || (q.warp == p.warp && q.symmetry == p.symmetry && q.zoom == p.zoom &&
                                                              q.temperature == p.temperature && q.morph == p.morph),
          "off: apply changes nothing");
    // Switched off mid-song: eases out, then is exactly off again.
    NoteResponse fading = on();
    run(fading, 4.0f, melody);
    fading.configure({false, 7});
    run(fading, 0.2f, melody);
    check(fading.amount() > 0.0f && fading.amount() < 1.0f, "switching off eases out");
    run(fading, 8.0f, melody);
    check(fading.amount() == 0.0f, "then it is off");
    for (float k : fading.uniforms().knobs) check(k == 0.5f, "and every knob is back at 0.5");
    check(fading.uniforms().bloomCount == 0, "and no bloom is left");
}

void silenceIsStable() {
    NoteResponse n = on();
    run(n, 20.0f, [](float) { return GeodeSoundFrame{}; }, Emotion::State{});
    const NoteUniforms& u = n.uniforms();
    for (float k : u.knobs) check(std::isfinite(k) && k >= 0.0f && k <= 1.0f, "silence: knobs finite and in range");
    for (float h : u.hues) check(std::isfinite(h) && h >= 0.0f && h < 1.0f, "silence: hues finite");
    check(u.bloomCount == 0, "silence: no blooms");
    check(std::isfinite(n.percussiveness()) && std::fabs(n.percussiveness() - 0.5f) < 0.2f, "silence: drum weight undecided");
    check(std::fabs(n.swing()) < 1e-3f && n.distortion() < 1e-3f, "silence: still");
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
    n.step(bad, Emotion::State{}, {NAN, NAN}, kDt);
    run(n, 1.0f, [](float) { return GeodeSoundFrame{}; }, Emotion::State{});
    for (float k : n.uniforms().knobs) check(std::isfinite(k), "a NaN frame leaves the knobs finite");
    check(std::isfinite(n.swing()) && std::isfinite(n.anchorHue()) && std::isfinite(n.distortion()) &&
              std::isfinite(n.percussiveness()),
          "a NaN frame leaves the motion finite");
    const SceneParams q = n.apply(SceneParams{});
    check(std::isfinite(q.warp) && std::isfinite(q.twist) && std::isfinite(q.morph) && std::isfinite(q.temperature),
          "a NaN frame leaves the params finite");
}

void newPlayForgets() {
    NoteResponse n = on(1);
    run(n, 3.0f, melody);
    check(n.spawned() > 0, "blooms before the new play");
    n.configure({true, 2});
    check(n.spawned() == 0 && n.liveBlooms() == 0, "a new seed is a new play");
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

// One long note: a single flower opening fully.
GeodeSoundFrame held(float t) {
    GeodeSoundFrame f = base();
    f.pitch = 72.0f;
    f.pitchClass = 0.0f;
    f.pitchHeight = 0.75f;
    f.pitchConfidence = 0.9f;
    f.tension = 0.55f;
    if (t < kDt) {
        f.noteChange = 1.0f;
        f.interval = 0.5f;
        f.onsetEnv = 1.0f;
    }
    return f;
}

// A fast run, eight notes a second, up two octaves of a pentatonic scale and back.
GeodeSoundFrame fastRun(float t) {
    static const int kRun[] = {0, 2, 4, 7, 9, 12, 14, 16, 19, 21, 24, 21, 19, 16, 14, 12, 9, 7, 4, 2};
    GeodeSoundFrame f = base();
    const int i = static_cast<int>(t / 0.125f);
    const int note = kRun[i % 20];
    const int prev = kRun[(i + 19) % 20];
    f.pitch = 55.0f + note;
    f.pitchClass = static_cast<float>((55 + note) % 12) / 12.0f;
    f.pitchHeight = (f.pitch - 36.0f) / 48.0f;
    f.runDensity = 0.7f;
    if (std::fmod(t, 0.125f) < kDt && i > 0) {
        f.noteChange = 0.7f;
        f.interval = static_cast<float>(note - prev) / 12.0f;
        f.onsetEnv = 0.6f;
    }
    return f;
}

std::string jsonList(const float* v, int n) {
    std::string s = "[";
    for (int i = 0; i < n; ++i) s += (i ? ", " : "") + std::to_string(v[i]);
    return s + "]";
}

// tools/glslcheck --bloom reads this: the uniforms NoteBloomPass would upload after `seconds` of a
// scenario, so the preview stills are what the engine decided rather than hand-made stand-ins.
int dump(const std::string& scenario, float seconds) {
    std::function<GeodeSoundFrame(float)> make = melody;
    Emotion::State e = calm();
    if (scenario == "minor") {
        make = minorLeaps;
        e.valence = -0.4f;
        e.tension = 0.7f;
    } else if (scenario == "held") {
        make = held;
    } else if (scenario == "run") {
        make = fastRun;
        e.energy = 0.8f;
    } else if (scenario != "melody") {
        std::printf("unknown scenario %s (melody, minor, held, run)\n", scenario.c_str());
        return 2;
    }
    NoteResponse n = on(20261101);
    run(n, seconds, make, e);
    const NoteUniforms& u = n.uniforms();
    constexpr int kRows = NoteUniforms::kBlooms * 4;
    std::printf("{\"uBloomCount\": %d, \"uBloomGain\": %.3f,\n", u.bloomCount, 0.9);
    std::printf(" \"uBloomA\": %s,\n", jsonList(u.bloomA[0].data(), kRows).c_str());
    std::printf(" \"uBloomB\": %s,\n", jsonList(u.bloomB[0].data(), kRows).c_str());
    std::printf(" \"uBloomC\": %s,\n", jsonList(u.bloomC[0].data(), kRows).c_str());
    std::printf(" \"uNoteHues\": %s}\n", jsonList(u.hues.data(), NoteUniforms::kHues).c_str());
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 4 && std::strcmp(argv[1], "--dump") == 0) return dump(argv[2], static_cast<float>(std::atof(argv[3])));
    knobsAnswerSeveralFields();
    drumWeightFollowsTheInput();
    bloomsFollowNotes();
    chordChangesShiftTheScene();
    tensionDistortsAndResolves();
    intervalsMove();
    keyAndMood();
    offIsToday();
    silenceIsStable();
    newPlayForgets();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("note response: all checks passed\n");
    return 0;
}
