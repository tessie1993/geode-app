// Host test of the auto-director: the emotion table (tools/music_emotion.py), the live feeling
// (sound/Emotion) and the director's decisions (viz/Director): when it decides, that it never touches a colour
// param and holds its look still between decisions, that happy and sad stay apart whatever the seed, and that
// it is deterministic. Then the director's behaviour axes (director::applyAxes, axisMoves).
// Built by CI (.github/workflows/native-host-tests.yml) with core/sound/*.cpp and the viz sources
// Director.cpp, Reactions.cpp, Lfo.cpp, CameraRig.cpp, Params.cpp, ParamsFields.cpp and VisualSafety.cpp.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>

#include "sound/Emotion.hpp"
#include "sound/Form.hpp"
#include "sound/Harmony.hpp"
#include "viz/CameraRig.hpp"
#include "viz/Director.hpp"

using geode::sound::Emotion;
using geode::viz::CameraRig;
using geode::viz::Director;
using geode::viz::DirectorConfig;
using geode::viz::SceneParams;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL %s\n", what.c_str());
        ++failures;
    }
}

int scaleIndex(const char* id) {
    for (int i = 0; i < geode::sound::scaleCount(); ++i) {
        if (std::strcmp(geode::sound::scaleInfo(i).id, id) == 0) return i;
    }
    std::printf("no scale %s\n", id);
    std::exit(1);
}

struct Song {
    const char* scale;
    int root;
    float loudness;
    float bpm;
    float centroid;
    float onsetEvery;  // seconds between onsets
};

// A steady frame of a song in one key and scale, at time t.
GeodeSoundFrame frame(const Song& s, float t, float dt) {
    GeodeSoundFrame f{};
    f.loudness = s.loudness;
    f.level = 0.6f;
    f.scale = static_cast<float>(scaleIndex(s.scale));
    f.scaleRoot = s.root / 12.0f;
    f.scaleFit = 0.8f;
    f.keyRoot = s.root / 12.0f;
    f.keyClarity = 0.7f;
    f.keyMajor = 1.0f;
    f.bpm = s.bpm;
    f.beatConf = 0.8f;
    f.centroid = s.centroid;
    f.energyTrend = 0.5f;
    f.tension = 0.15f;
    const float phase = std::fmod(t, s.onsetEvery);
    f.onset = phase < dt ? 1.0f : 0.0f;
    const float beat = 60.0f / s.bpm;
    const float bt = std::fmod(t, beat * 4.0f);
    f.downbeat = bt < dt ? 1.0f : 0.0f;
    for (int i = 0; i < 12; ++i) f.chromaFast[i] = 0.2f;
    f.chromaFast[s.root % 12] = 1.0f;
    return f;
}

const Song kHappy{"ionian", 0, 0.85f, 132.0f, 0.6f, 0.2f};
const Song kSad{"aeolian", 9, 0.55f, 72.0f, 0.3f, 1.2f};

constexpr float kDt = 1.0f / 60.0f;

void run(Director& d, const Song& s, float seconds, float& t) {
    for (float end = t + seconds; t < end; t += kDt) d.step(frame(s, t, kDt), kDt);
}

void testTable() {
    check(Emotion::modeCount() == geode::sound::scaleCount(), "a mode vector per scale");
    const auto v = [](const char* id) { return Emotion::mode(scaleIndex(id)).valence; };
    check(v("ionian") > v("dorian") && v("dorian") > v("aeolian") && v("aeolian") > v("phrygian") && v("phrygian") > v("locrian") - 1e-6f,
          "valence falls as degrees are lowered (Temperley & Tan 2013)");
    check(v("lydian") > 0.3f && v("mixolydian") > 0.0f, "the major-third modes read happy");
    check(Emotion::mode(scaleIndex("locrian")).tension > Emotion::mode(scaleIndex("ionian")).tension, "Locrian is tenser than Ionian");
    check(Emotion::mode(scaleIndex("major_pentatonic")).tension < 0.1f, "the major pentatonic is at rest");
    const auto& lyd = Emotion::mode(scaleIndex("lydian")).gems;
    int top = 0;
    for (int i = 1; i < Emotion::kGems; ++i) top = lyd[i] > lyd[top] ? i : top;
    check(top == Emotion::kWonder, "Lydian's leading feeling is wonder");
    for (int i = 0; i < Emotion::modeCount(); ++i) {
        float sum = 0.0f;
        for (float g : Emotion::mode(i).gems) sum += g;
        check(std::fabs(sum - 1.0f) < 0.01f, "mode GEMS sum to one");
    }
    // Every key + scale is its own feeling.
    std::set<std::string> prints;
    for (int sc = 0; sc < Emotion::modeCount(); ++sc) {
        for (int r = 0; r < 12; ++r) {
            const auto fp = Emotion::fingerprint(sc, r);
            char key[256];
            int n = std::snprintf(key, sizeof key, "%.3f", fp.sharpness);
            for (float g : fp.gems) n += std::snprintf(key + n, sizeof key - n, ",%.4f", g);
            n += std::snprintf(key + n, sizeof key - n, "|%.3f,%.3f", fp.valence, fp.tension);
            prints.insert(key);
        }
    }
    check(static_cast<int>(prints.size()) == Emotion::modeCount() * 12, "552 distinct fingerprints, got " + std::to_string(prints.size()));
    // The key's tradition never moves valence.
    check(Emotion::fingerprint(0, 5).valence == Emotion::fingerprint(0, 2).valence, "the key leaves valence to the mode");
}

void testLiveFeeling() {
    Emotion happy;
    Emotion sad;
    float t = 0.0f;
    for (; t < 20.0f; t += kDt) {
        happy.step(frame(kHappy, t, kDt), kDt);
        sad.step(frame(kSad, t, kDt), kDt);
    }
    const auto& h = happy.state();
    const auto& s = sad.state();
    std::printf("happy v %.2f e %.2f t %.2f | sad v %.2f e %.2f t %.2f\n", h.valence, h.energy, h.tension, s.valence, s.energy, s.tension);
    check(h.valence > 0.2f, "a loud fast major song reads happy");
    check(s.valence < -0.2f, "a quiet slow minor song reads sad");
    check(h.energy > s.energy + 0.2f, "and more energetic");
    check(h.gems[Emotion::kJoy] > s.gems[Emotion::kJoy] && s.gems[Emotion::kSadness] > h.gems[Emotion::kSadness], "joy against sadness");
    check(h.degree == 0 && s.degree == 0, "the leading note is the tonic");
}

void testPacing() {
    Director d;
    d.configure({1.0f, 42});
    float t = 0.0f;
    run(d, kHappy, 3.0f, t);
    check(d.commits() == 0, "no look before four seconds of music");
    run(d, kHappy, 3.0f, t);
    check(d.commits() == 1 && d.lastReason() == Director::Reason::First, "the first look once the key is trusted");
    run(d, kHappy, 30.0f, t);
    check(d.commits() == 1, "no decisions inside a steady section");
    check(d.weight() > 0.9f, "the director is fully in by now");
    // A new section.
    GeodeSoundFrame f = frame(kHappy, t, kDt);
    f.sectionChange = 1.0f;
    d.step(f, kDt);
    t += kDt;
    check(d.commits() == 2 && d.lastReason() == Director::Reason::Section, "a new section is a decision");
    f.sectionChange = 1.0f;
    d.step(f, kDt);
    t += kDt;
    check(d.commits() == 2, "a section change right after a decision is not another");
    run(d, kHappy, 1.0f, t);
    f = frame(kHappy, t, kDt);
    f.drop = 1.0f;
    d.step(f, kDt);
    t += kDt;
    check(d.commits() == 3 && d.lastReason() == Director::Reason::Drop, "a drop always decides");
    run(d, kHappy, 0.3f, t);
    check(d.look().axes == d.target().axes && d.look().camSmooth == d.target().camSmooth, "a drop cuts in a quarter second");
    // A modulation that holds.
    run(d, Song{"aeolian", 4, 0.85f, 132.0f, 0.6f, 0.2f}, 10.0f, t);
    check(d.commits() == 4 && d.lastReason() == Director::Reason::Modulation, "a held modulation decides on a downbeat");
    // A new seed is a new play.
    d.configure({1.0f, 43});
    check(d.commits() == 0, "a new seed starts over");
}

// Form owns the event floor (Form::kEventFloor, 0.4) and the director acts on every event Form fires. Before,
// it re-thresholded the strength at 0.5 and missed the events between: a drop after a build that peaked at
// 0.45, or a section change at novelty 0.45, changed nothing on screen.
void testWeakEventsDecide() {
    const float weak = 0.45f;
    check(weak >= geode::sound::Form::kEventFloor, "0.45 is an event Form can fire");
    {
        Director d;
        d.configure({1.0f, 42});
        float t = 0.0f;
        run(d, kHappy, 20.0f, t);
        check(d.commits() == 1, "one first look before the weak section change");
        GeodeSoundFrame f = frame(kHappy, t, kDt);
        f.sectionChange = weak;
        d.step(f, kDt);
        check(d.commits() == 2 && d.lastReason() == Director::Reason::Section, "a section change at 0.45 commits a look");
    }
    {
        Director d;
        d.configure({1.0f, 42});
        float t = 0.0f;
        run(d, kHappy, 20.0f, t);
        GeodeSoundFrame f = frame(kHappy, t, kDt);
        f.drop = weak;
        d.step(f, kDt);
        check(d.commits() == 2 && d.lastReason() == Director::Reason::Drop, "a drop at 0.45 commits a look");
    }
    {
        // No event, no decision: the frame between events holds 0, whatever else moves.
        Director d;
        d.configure({1.0f, 42});
        float t = 0.0f;
        run(d, kHappy, 20.0f, t);
        GeodeSoundFrame f = frame(kHappy, t, kDt);
        check(!geode::sound::dropped(f) && !geode::sound::sectionChanged(f), "a plain frame carries no event");
        d.step(f, kDt);
        check(d.commits() == 1, "and decides nothing");
    }
}

constexpr size_t kEnergy = static_cast<size_t>(Director::Axis::Energy);

bool colourParam(const std::string& name) {
    for (const char* word : {"palette", "Palette", "hue", "Hue", "color", "Color", "saturation", "brightness", "temperature", "contrast", "Tint"}) {
        if (name.find(word) != std::string::npos) return true;
    }
    return false;
}

// The three params a look may move. Everything else in SceneParams, every colour among it, is the style's
// and the user's.
bool lookParam(const std::string& name) {
    return name == "camSmooth" || name == "motionBreath" || name == "motionDrift";
}

// The owner's rule, "no colour narrowing": the looks used to draw every style's palette from nine hue
// anchors and one saturation curve, so under Auto every style wore the same few colours. Whatever the
// scale, key, song and seed, a look now leaves every param but its three motion dials exactly as given, and
// its axes never move a colour param.
void testNeverTouchesColour() {
    const Song songs[] = {{"ionian", 0, 0.85f, 132.0f, 0.6f, 0.2f}, {"aeolian", 0, 0.55f, 72.0f, 0.3f, 1.2f}, {"dorian", 0, 0.7f, 100.0f, 0.45f, 0.5f}};
    SceneParams user;
    user.palette = 6;
    user.paletteBaseOverride = 0.31f;
    user.paletteRangeOverride = 0.22f;
    user.hueRange = 0.7f;
    user.saturation = 0.6f;
    user.brightness = 1.1f;
    user.temperature = -0.2f;
    user.contrast = 0.9f;
    user.colorShift = 0.15f;
    user.milkdropPaletteTint = 0.3f;
    int looks = 0;
    int moved = 0;
    for (int scale = 0; scale < geode::sound::scaleCount(); ++scale) {
        for (int root = 0; root < 12; root += 4) {
            for (const Song& base : songs) {
                Song s = base;
                s.scale = geode::sound::scaleInfo(scale).id;
                s.root = root;
                Director d;
                d.configure({1.0f, static_cast<uint64_t>(104729 * (scale + 1) + root)});
                float t = 0.0f;
                run(d, s, 15.0f, t);
                GeodeSoundFrame f = frame(s, t, kDt);
                f.drop = 1.0f;
                d.step(f, kDt);
                t += kDt;
                run(d, s, 1.0f, t);
                for (const SceneParams& in : {SceneParams{}, user}) {
                    const SceneParams out = d.apply(in);
                    for (const auto& spec : SceneParams::schema()) {
                        const std::string name(spec.name);
                        const bool same = out.get(name) == in.get(name);
                        if (lookParam(name)) {
                            if (!same) ++moved;
                        } else if (!same) {
                            check(false, "a look moved " + name + " (" + s.scale + " root " + std::to_string(root) + ")");
                            return;
                        }
                    }
                }
                for (const SceneParams& in : {SceneParams{}, user}) {
                    const SceneParams out = d.applyAxes(in);
                    for (const auto& spec : SceneParams::schema()) {
                        const std::string name(spec.name);
                        if (colourParam(name) && out.get(name) != in.get(name)) {
                            check(false, "a look's axes moved " + name + " (" + s.scale + ")");
                            return;
                        }
                    }
                }
                ++looks;
            }
        }
    }
    check(looks > 300 && moved > looks, "the looks tried do drive their own three dials");
}

// "No hardcoded reactions": a look is decided, glided in and then held. Between decisions nothing the
// director hands on (params, axes, camera moves) follows the frame, however hard the drums, the melody
// or a build push: what moves then is the reaction matrix alone.
void testHeldBetweenDecisions() {
    Director d;
    d.configure({1.0f, 42});
    float t = 0.0f;
    run(d, kHappy, 20.0f, t);
    check(d.commits() == 1 && d.weight() > 0.99f, "one look, fully in");
    const SceneParams p;
    const SceneParams held = d.apply(p);
    const Director::Axes heldAxes = d.axes();
    const CameraRig::Moves heldMoves = d.moves();
    bool still = true;
    float maxAxis = 0.0f;
    for (int i = 0; i < 1200; ++i, t += kDt) {
        GeodeSoundFrame f = frame(kHappy, t, kDt);
        f.kick = f.kickEnv = i % 30 == 0 ? 1.0f : 0.0f;
        f.snare = f.snareEnv = i % 60 == 30 ? 1.0f : 0.0f;
        f.bassAtt = 1.0f + 1.5f * std::fabs(std::sin(0.3f * i));
        f.buildUp = (i % 600) / 600.0f;
        f.energyTrend = 0.5f + 0.5f * std::sin(0.01f * i);
        f.noteChange = i % 20 == 0 ? 1.0f : 0.0f;
        f.pitchHeight = 0.5f + 0.5f * std::sin(0.05f * i);
        f.intensity = 0.5f + 0.5f * std::sin(0.02f * i);
        d.step(f, kDt);
        const SceneParams now = d.apply(p);
        const Director::Axes x = d.axes();
        const CameraRig::Moves m = d.moves();
        for (int j = 0; j < Director::kAxes; ++j) maxAxis = std::max(maxAxis, std::fabs(x[j] - heldAxes[j]));
        still = still && std::fabs(now.camSmooth - held.camSmooth) < 1e-3f && std::fabs(now.motionBreath - held.motionBreath) < 1e-3f &&
                std::fabs(m.shake - heldMoves.shake) < 1e-3f && std::fabs(m.orbit - heldMoves.orbit) < 1e-3f;
    }
    check(d.commits() == 1, "no decision in those twenty seconds");
    check(still && maxAxis < 1e-3f, "between decisions the look holds still under drums, melody and a build (axis drift " + std::to_string(maxAxis) + ")");
}

// A look glides in over a bar of the tempo the tracker hears. With no tempo heard none is assumed (no
// stand-in tempo): the glide is the same whatever stale bpm the frame carries.
void testNoTempoAssumed() {
    float glides[3] = {};
    const float bpms[3] = {0.0f, 60.0f, 240.0f};
    for (int n = 0; n < 3; ++n) {
        Director d;
        d.configure({1.0f, 42});
        float t = 0.0f;
        run(d, kHappy, 20.0f, t);
        GeodeSoundFrame f = frame(kHappy, t, kDt);
        f.bpm = bpms[n];
        f.beatConf = 0.0f;
        f.downbeat = 0.0f;
        f.sectionChange = 1.0f;
        d.step(f, kDt);
        const float goal = d.target().axes[kEnergy];
        const float start = d.look().axes[kEnergy];
        for (int i = 0; i < 600 && std::fabs(d.look().axes[kEnergy] - goal) > 1e-4f * (1.0f + std::fabs(goal - start)); ++i) {
            t += kDt;
            f = frame(kHappy, t, kDt);
            f.bpm = bpms[n];
            f.beatConf = 0.0f;
            f.downbeat = 0.0f;
            d.step(f, kDt);
            glides[n] += kDt;
        }
    }
    check(glides[0] == glides[1] && glides[1] == glides[2], "with no beat heard the glide does not depend on a bpm");
    check(glides[0] > 1.5f && glides[0] < 2.1f, "and is a plain time in seconds, the middle of the 1..4 s glide range (" + std::to_string(glides[0]) + ")");
}

void testFeelingSurvivesTheSeed() {
    // Whatever the seed, a happy look runs hotter than a sad one: the feeling decides, the seed only varies.
    int hotter = 0;
    std::set<int> variety;
    for (uint64_t seed = 1; seed <= 20; ++seed) {
        Director h;
        Director s;
        h.configure({1.0f, seed});
        s.configure({1.0f, seed});
        float t1 = 0.0f;
        float t2 = 0.0f;
        run(h, kHappy, 12.0f, t1);
        run(s, kSad, 12.0f, t2);
        if (h.target().axes[kEnergy] > s.target().axes[kEnergy] + 0.3f) ++hotter;
        variety.insert(static_cast<int>(h.target().axes[kEnergy] * 100.0f));
    }
    check(hotter == 20, "happy drives the Energy axis above sad, every seed (" + std::to_string(hotter) + " of 20)");
    check(variety.size() >= 4, "and each play of the same song is its own look");
}

void testDeterministicAndOff() {
    Director a;
    Director b;
    a.configure({1.0f, 99});
    b.configure({1.0f, 99});
    float t1 = 0.0f;
    float t2 = 0.0f;
    run(a, kSad, 15.0f, t1);
    run(b, kSad, 15.0f, t2);
    check(std::memcmp(&a.look(), &b.look(), sizeof(Director::Look)) == 0, "same seed and sound, same look");
    // The look turns the user's motion dials by a factor, never puts its own in their place: at full
    // weight a slider still moves the picture, and a dial at 0 stays off.
    SceneParams p;
    SceneParams soft = p;
    soft.motionBreath = 0.5f * p.motionBreath;
    SceneParams none = p;
    none.motionBreath = none.motionDrift = 0.0f;
    const SceneParams q = a.apply(p);
    const SceneParams qSoft = a.apply(soft);
    const SceneParams qNone = a.apply(none);
    check(q.motionBreath != p.motionBreath, "the look moves its motion dials");
    check(qSoft.motionBreath < q.motionBreath, "the user's step on a dial comes through the look");
    check(qNone.motionBreath == 0.0f && qNone.motionDrift == 0.0f, "a motion dial at 0 stays off under the look");
    Director off;
    off.configure({0.0f, 99});
    float t3 = 0.0f;
    run(off, kSad, 15.0f, t3);
    const SceneParams r = off.apply(p);
    check(std::memcmp(&r, &p, sizeof(SceneParams)) == 0, "off leaves the params alone");
    for (float v : off.axes()) check(v == 0.0f, "off turns no axis");
    const SceneParams ra = off.applyAxes(p);
    check(std::memcmp(&ra, &p, sizeof(SceneParams)) == 0, "and its axes leave the params alone");
    const CameraRig::Moves m = off.moves();
    check(m.orbit == 1.0f && m.lean == 1.0f && m.dolly == 1.0f && m.breath == 1.0f && m.shake == 1.0f && m.reframe == 1.0f &&
              m.cut == 1.0f,
          "and the camera's music moves");
    float t4 = 0.0f;
    run(a, kSad, 5.0f, t4);
    bool turned = false;
    for (float v : a.axes()) {
        check(v >= -1.0f && v <= 1.0f, "an axis stays in -1..1");
        turned = turned || v != 0.0f;
    }
    check(turned, "a director that is in turns its axes");
}

// ---- The director's axes (director::applyAxes, axisMoves) ----
// The rows the old macro knobs moved, kept for the director when the user's knobs became the bank:
// Energy, Speed, Complexity, Space and Chaos (Mood was never the director's).

using Axis = Director::Axis;

Director::Axes axis(Axis which, float v) {
    Director::Axes x{};
    x[static_cast<size_t>(which)] = v;
    return x;
}

void testAxesAtRestChangeNothing() {
    SceneParams p;
    p.speed = 1.7f;
    p.vignette = 0.3f;
    const SceneParams q = geode::viz::director::applyAxes(p, {});
    check(std::memcmp(&q, &p, sizeof(SceneParams)) == 0, "axes at rest leave the look as styled");
    const CameraRig::Moves m = geode::viz::director::axisMoves({});
    check(m.orbit == 1.0f && m.shake == 1.0f && m.cut == 1.0f, "axes at rest leave the camera's music moves as styled");
}

void testAxesTurnTowardTheEnds() {
    namespace dir = geode::viz::director;
    SceneParams p;
    const SceneParams up = dir::applyAxes(p, axis(Axis::Speed, 1.0f));
    check(std::fabs(up.speed - 4.0f) < 1e-5f, "a full Speed turn up reaches the top of the range");
    const SceneParams half = dir::applyAxes(p, axis(Axis::Speed, 0.5f));
    check(half.speed > p.speed && half.speed < up.speed, "half a turn goes part of the way");
    const SceneParams down = dir::applyAxes(p, axis(Axis::Speed, -1.0f));
    check(std::fabs(down.speed - 0.05f) < 1e-5f, "a full turn down reaches the bottom");
    const SceneParams wide = dir::applyAxes(p, axis(Axis::Space, 1.0f));
    check(wide.zoom < p.zoom && wide.vignette <= p.vignette && wide.camFov > p.camFov, "Space up widens: zoom out, less vignette, wider lens");
    SceneParams wild = p;
    wild.bloom = 9.0f;
    check(dir::applyAxes(wild, axis(Axis::Energy, 1.0f)).bloom == wild.bloom, "an axis never pushes a field further past its slider");
    check(dir::applyAxes(p, axis(Axis::Chaos, 1.0f)).speed == p.speed, "an axis only moves its own fields");
    check(dir::applyAxes(p, axis(Axis::Energy, 1.0f)).saturation == p.saturation, "no axis moves the colour: the palette is the look's");
}

// The value it is given is the base and the axis adds to it, so a slider still moves the picture under Auto.
void testAxesAddToTheSlider() {
    namespace dir = geode::viz::director;
    SceneParams low;
    low.speed = 0.4f;
    low.zoom = 0.8f;
    low.camFov = 0.8f;
    SceneParams high = low;
    high.speed = 0.9f;
    high.zoom = 1.3f;
    high.camFov = 1.0f;
    for (float turn : {0.3f, 0.6f, 1.0f, -0.3f}) {
        Director::Axes x{};
        x.fill(turn);
        const SceneParams a = dir::applyAxes(low, x);
        const SceneParams b = dir::applyAxes(high, x);
        check(b.speed > a.speed && b.camFov > a.camFov && b.zoom > a.zoom, "at any turn a higher slider still gives a higher value");
    }
    const Director::Axes quarter = axis(Axis::Speed, 0.25f);
    check(std::fabs((dir::applyAxes(high, quarter).speed - dir::applyAxes(low, quarter).speed) - (high.speed - low.speed)) < 1e-5f,
          "away from the ends the slider's whole step comes through");
    SceneParams back;
    back.camFly = -1.5f;
    check(dir::applyAxes(back, quarter).camFly < 0.0f, "a backward Fly speed still flies backward under a small Speed turn");
    SceneParams stopped;
    stopped.camFly = -1.0f;
    check(dir::applyAxes(stopped, axis(Axis::Speed, -1.0f)).camFly == stopped.camFly, "an axis never drags back a value already past its end");
}

// A slider between the default and the end keeps moving the picture under a turn: Auto's offset shrinks
// with the room left instead of pinning the param at the end, so every step up is still a step up.
void testAxesLeaveTheSliderRoom() {
    namespace dir = geode::viz::director;
    struct Row {
        const char* name;
        Axis axis;
        float SceneParams::*field;
        float from;
        float to;
    };
    const Row rows[] = {{"Intensity", Axis::Energy, &SceneParams::intensity, 1.0f, 1.25f},
                        {"Glow", Axis::Energy, &SceneParams::bloom, 0.0f, 0.25f},
                        {"Cycle speed", Axis::Speed, &SceneParams::cycleSpeed, 0.1f, 0.6f},
                        {"Speed", Axis::Speed, &SceneParams::speed, 1.0f, 4.0f}};
    for (const Row& r : rows) {
        for (float turn : {0.25f, 0.5f, 0.9f}) {
            float prev = -1e9f;
            bool rises = true;
            for (int i = 0; i < 20; ++i) {
                SceneParams p;
                p.*r.field = r.from + (r.to - r.from) * static_cast<float>(i) / 20.0f;
                const float o = dir::applyAxes(p, axis(r.axis, turn)).*r.field;
                rises = rises && o > prev && o <= r.to;
                prev = o;
            }
            check(rises, std::string(r.name) + ": every step of the slider up to the end still moves it under a turn");
        }
    }
    SceneParams top;
    top.intensity = 1.25f;
    check(dir::applyAxes(top, axis(Axis::Energy, 0.5f)).intensity == 1.25f, "a slider at the end stays there");
}

void testAxesScaleTheCamera() {
    namespace dir = geode::viz::director;
    check(dir::axisMoves(axis(Axis::Chaos, -1.0f)).cut == 0.0f, "Chaos all the way down stops hard cuts");
    check(dir::axisMoves(axis(Axis::Space, 1.0f)).orbit == 2.0f, "Space up doubles the orbit");
    SceneParams p;
    p.camFly = 1.0f;
    CameraRig rig;
    rig.setMoves(dir::axisMoves(axis(Axis::Chaos, -1.0f)));
    GeodeSoundFrame f{};
    f.drive = 1.0f;
    f.presence = 1.0f;
    f.intensity = 1.0f;
    f.bassAtt = 1.0f;
    for (int i = 0; i < 60; ++i) rig.step(p, {}, 1.0f / 60.0f, false, true, &f);
    f.hardCut = 1.0f;
    rig.step(p, {}, 1.0f / 60.0f, false, true, &f);
    check(rig.cuts() == 0, "with Chaos all the way down a big moment does not cut");
}

// The look is an offset on the user's dials, so a slider at either end of its range must not be
// pushed past it.
void testDialsStayInRange() {
    Director d;
    d.configure({1.0f, 99});
    float t = 0.0f;
    run(d, kSad, 15.0f, t);
    SceneParams low;
    low.camSmooth = low.motionBreath = low.motionDrift = 0.0f;
    SceneParams high;
    high.camSmooth = high.motionBreath = high.motionDrift = 1.0f;
    for (const SceneParams& user : {low, high}) {
        const SceneParams o = d.apply(user);
        const auto in = [](float v, float lo, float hi) { return v >= lo && v <= hi; };
        check(in(o.camSmooth, 0.0f, 1.0f) && in(o.motionBreath, 0.0f, 1.0f) && in(o.motionDrift, 0.0f, 1.0f),
              "the look never pushes a dial past its range");
    }
}

// The beat tracker's downbeat carries its confidence as a strength, and it locks at 0.35. Counting only
// downbeats above 0.5 meant that on a beat held at, say, 0.4 no bar was ever counted and a modulation
// never landed.
void testWeakDownbeatsCount() {
    Director d;
    d.configure({1.0f, 42});
    float t = 0.0f;
    run(d, kHappy, 6.0f, t);
    check(d.commits() == 1, "one first look before the modulation");
    const Song moved{"aeolian", 4, 0.85f, 132.0f, 0.6f, 0.2f};
    for (float end = t + 10.0f; t < end; t += kDt) {
        GeodeSoundFrame f = frame(moved, t, kDt);
        f.downbeat *= 0.4f;
        d.step(f, kDt);
    }
    check(d.commits() == 2 && d.lastReason() == Director::Reason::Modulation, "a modulation lands on a downbeat of strength 0.4");
}

// A new track is a new play, but the picture must not fall back to the user's bare settings while the
// new play waits to make its first decision: the look on screen is carried, and glided from.
void testLookCarriesAcrossPlays() {
    Director d;
    d.configure({1.0f, 5});
    float t = 0.0f;
    run(d, kHappy, 15.0f, t);
    const float weight = d.weight();
    const float energy = d.look().axes[kEnergy];
    check(weight > 0.9f, "the look is fully in after fifteen seconds");
    d.configure({1.0f, 6});
    check(d.commits() == 0, "a new seed still starts the decisions over");
    d.step(frame(kSad, t, kDt), kDt);
    t += kDt;
    check(d.weight() >= weight - 1e-3f && std::fabs(d.look().axes[kEnergy] - energy) < 1e-4f, "the look stays on screen into the next track");
    run(d, kSad, 13.0f, t);
    check(d.commits() >= 1 && d.weight() > 0.9f, "and the new play's first look takes over from it");
}

// Music with no sections and no drops (ambient, a steady groove) still gets a new style now and then.
void testLongStretchStepsTheStyle() {
    Director d;
    d.configure({1.0f, 42});
    float t = 0.0f;
    run(d, kHappy, 6.0f, t);
    check(d.commits() == 1 && !d.styleMoment(), "the first look is not a style change");
    run(d, kHappy, 76.0f, t);
    check(d.commits() == 2 && d.lastReason() == Director::Reason::Long && d.styleMoment(), "75 s with no section steps the style");
}

// With Auto off the director still steps (Note & key reactions read its feeling) and still commits, but
// none of those commits may step the user's style: that is what claimed the style with Note & key on.
void testNoStyleMomentWhenOff() {
    for (const float amount : {0.0f, 1.0f}) {
        Director d;
        d.configure({amount, 42});
        float t = 0.0f;
        run(d, kHappy, 12.0f, t);
        run(d, kHappy, 30.0f, t);
        GeodeSoundFrame f = frame(kHappy, t, kDt);
        f.sectionChange = 1.0f;
        const int before = d.commits();
        d.step(f, kDt);
        check(d.commits() == before + 1 && d.lastReason() == Director::Reason::Section, "a section commits either way");
        check(d.styleMoment() == (amount > 0.0f), amount > 0.0f ? "Auto on: a section steps the style" : "Auto off: a section leaves the style alone");
    }
}

// The owner's report: "almost all visuals have a yin-yang curve and all seem to behave similarly".
// Twist spirals any style about the screen centre, and the director's Complexity axis used to hold
// it bent on every style: toward +1 for a busy feeling, toward -1 for a calm one. Whatever the key,
// scale, song and seed, the look must leave an untouched style untwisted and a twisted one as set.
void testNoLookTwistsEveryStyle() {
    const Song songs[] = {{"ionian", 0, 0.85f, 132.0f, 0.6f, 0.2f}, {"aeolian", 0, 0.55f, 72.0f, 0.3f, 1.2f}, {"dorian", 0, 0.7f, 100.0f, 0.45f, 0.5f}};
    int looks = 0;
    int turned = 0;
    for (int scale = 0; scale < geode::sound::scaleCount(); ++scale) {
        for (int root = 0; root < 12; root += 4) {
            for (const Song& base : songs) {
                Song s = base;
                s.scale = geode::sound::scaleInfo(scale).id;
                s.root = root;
                Director d;
                d.configure({1.0f, static_cast<uint64_t>(7919 * (scale + 1) + root)});
                float t = 0.0f;
                run(d, s, 15.0f, t);
                const Director::Axes x = d.axes();
                if (std::fabs(x[static_cast<size_t>(Axis::Complexity)]) > 0.2f) ++turned;
                SceneParams styled;
                SceneParams user;
                user.twist = -0.3f;
                const float a = d.applyAxes(d.apply(styled)).twist;
                const float b = d.applyAxes(d.apply(user)).twist;
                if (a != 0.0f || b != -0.3f) {
                    check(false, std::string("a look twists the picture: ") + s.scale + " root " + std::to_string(root) + " twist " + std::to_string(a));
                    return;
                }
                ++looks;
            }
        }
    }
    check(looks > 300 && turned > looks / 10, "the looks tried do turn the Complexity axis, both ways");
}

// The same rule on the axes' pure maths (it was viz_math_test's check on the old macro knobs, whose
// rows the director's axes took over): Twist bends every style's picture the same way, so an axis
// that holds it bent stamps one S-curve on every style. Twist rests in the middle of its range, so
// either direction of Complexity used to twist. Twist is the user's slider and the style's own
// reactions, never an axis's.
void testAxesNeverHoldThePictureTwisted() {
    namespace dir = geode::viz::director;
    SceneParams styled;
    SceneParams turned;
    turned.twist = 0.3f;
    for (int a = 0; a < Director::kAxes; ++a) {
        for (float turn : {-1.0f, -0.4f, 0.4f, 1.0f}) {
            const Director::Axes x = axis(static_cast<Axis>(a), turn);
            check(dir::applyAxes(styled, x).twist == 0.0f, "no axis twists an untwisted style, either way");
            check(dir::applyAxes(turned, x).twist == 0.3f, "and the Twist slider is the user's alone");
        }
    }
}
}  // namespace

int main() {
    testTable();
    testLiveFeeling();
    testPacing();
    testWeakEventsDecide();
    testNeverTouchesColour();
    testHeldBetweenDecisions();
    testNoTempoAssumed();
    testFeelingSurvivesTheSeed();
    testDeterministicAndOff();
    testNoStyleMomentWhenOff();
    testDialsStayInRange();
    testWeakDownbeatsCount();
    testLookCarriesAcrossPlays();
    testLongStretchStepsTheStyle();
    testAxesAtRestChangeNothing();
    testAxesTurnTowardTheEnds();
    testAxesAddToTheSlider();
    testAxesLeaveTheSliderRoom();
    testAxesScaleTheCamera();
    testNoLookTwistsEveryStyle();
    testAxesNeverHoldThePictureTwisted();
    if (failures) {
        std::printf("director_test: %d failed\n", failures);
        return 1;
    }
    std::printf("director_test: all passed\n");
    return 0;
}
