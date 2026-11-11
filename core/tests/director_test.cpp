// Host test of the auto-director: the emotion table (tools/music_emotion.py), the live feeling
// (sound/Emotion) and the director's decisions (viz/Director): when it decides, that the same feeling never
// gives the same picture, that happy and sad stay apart whatever the seed, and that it is deterministic.
// Built by CI (.github/workflows/android.yml, "Director test") with core/sound/*.cpp and the viz sources
// Director.cpp, Reactions.cpp, Lfo.cpp, Macros.cpp, CameraRig.cpp, Params.cpp and ParamsFields.cpp.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>

#include "sound/Emotion.hpp"
#include "sound/Form.hpp"
#include "sound/Harmony.hpp"
#include "viz/Director.hpp"

using geode::sound::Emotion;
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
    check(std::fabs(d.look().hue - d.target().hue) < 1e-3f, "a drop cuts in a quarter second");
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

float distance(const Director::Look& a, const Director::Look& b) {
    const float dh = std::fabs(std::fmod(a.hue - b.hue + 1.5f, 1.0f) - 0.5f);
    return 2.0f * dh + std::fabs(a.range - b.range) + 0.5f * std::fabs(a.saturation - b.saturation);
}

void testNeverTheSame() {
    Director d;
    d.configure({1.0f, 7});
    float t = 0.0f;
    run(d, kHappy, 8.0f, t);
    std::vector<Director::Look> looks{d.target()};
    for (int i = 0; i < 12; ++i) {
        run(d, kHappy, 5.0f, t);
        GeodeSoundFrame f = frame(kHappy, t, kDt);
        f.sectionChange = 1.0f;
        d.step(f, kDt);
        t += kDt;
        looks.push_back(d.target());
    }
    float nearest = 1e9f;
    for (size_t i = 1; i < looks.size(); ++i) {
        for (size_t j = i >= 6 ? i - 6 : 0; j < i; ++j) nearest = std::min(nearest, distance(looks[i], looks[j]));
    }
    std::printf("same feeling, 13 looks: nearest of any six in a row %.3f\n", nearest);
    check(nearest > 0.1f, "the same feeling never repeats a look within six decisions");
}

void testFeelingSurvivesTheSeed() {
    // Whatever the seed, happy looks are brighter and more saturated than sad ones (Palmer et al. 2013).
    float happyBright = 0.0f;
    float sadBright = 0.0f;
    float happySat = 0.0f;
    float sadSat = 0.0f;
    std::set<int> hues;
    for (uint64_t seed = 1; seed <= 20; ++seed) {
        Director h;
        Director s;
        h.configure({1.0f, seed});
        s.configure({1.0f, seed});
        float t1 = 0.0f;
        float t2 = 0.0f;
        run(h, kHappy, 12.0f, t1);
        run(s, kSad, 12.0f, t2);
        happyBright += h.target().brightness;
        sadBright += s.target().brightness;
        happySat += h.target().saturation;
        sadSat += s.target().saturation;
        hues.insert(static_cast<int>(h.target().hue * 24.0f));
    }
    std::printf("bright %.2f vs %.2f, saturation %.2f vs %.2f, %zu hue bins over 20 plays\n", happyBright / 20, sadBright / 20,
                happySat / 20, sadSat / 20, hues.size());
    check(happyBright > sadBright + 0.5f, "happy is lighter than sad, every seed");
    check(happySat > sadSat + 1.0f, "happy is more saturated than sad, every seed");
    check(hues.size() >= 4, "and each play of the same song is its own palette");
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
    SceneParams p;
    const SceneParams q = a.apply(p);
    check(q.paletteBaseOverride >= 0.0f && q.paletteRangeOverride >= 0.0f, "the look sets the palette");
    // Both hue conventions land on the look's hue: pal() runs the wheel backwards, the simulations forward.
    const auto near = [](float a, float b) { return std::fabs(std::fmod(a - b + 1.5f, 1.0f) - 0.5f) < 0.03f; };
    const float hue = a.look().hue;
    check(near(-q.paletteBaseOverride - 0.5f * q.paletteRangeOverride, hue), "shader styles get the look's hue");
    const SceneParams sim = a.apply(p, false);
    check(near(sim.paletteBaseOverride + 0.5f * sim.paletteRangeOverride, hue), "simulations get the same hue");
    // The look is added to the user's dials, never put in their place: at full weight a slider still moves the picture.
    SceneParams dim = p;
    dim.brightness = p.brightness - 0.3f;
    dim.saturation = p.saturation - 0.4f;
    dim.contrast = p.contrast - 0.2f;
    const SceneParams qDim = a.apply(dim);
    check(std::fabs((q.brightness - qDim.brightness) - 0.3f) < 1e-5f && std::fabs((q.saturation - qDim.saturation) - 0.4f) < 1e-5f &&
              std::fabs((q.contrast - qDim.contrast) - 0.2f) < 1e-5f,
          "the user's Brightness, Saturation and Contrast steps come through the look whole");
    // The palette too: another palette under the same look is another picture, turned by as much as
    // the two palettes differ.
    SceneParams forest = p;
    forest.palette = 6;
    const SceneParams qForest = a.apply(forest);
    const auto drawn = [](const SceneParams& sp) { return -sp.paletteBaseOverride - 0.5f * sp.paletteRangeOverride; };
    const auto picked = [](const SceneParams& sp) { return -sp.paletteBase() - 0.5f * sp.paletteRange(); };
    check(near(drawn(qForest) - drawn(q), picked(forest) - picked(p)), "the user's palette pick comes through the look whole");
    check(!near(drawn(qForest), drawn(q)), "so the palette pickers still change the picture with Auto on");
    Director off;
    off.configure({0.0f, 99});
    float t3 = 0.0f;
    run(off, kSad, 15.0f, t3);
    const SceneParams r = off.apply(p);
    check(std::memcmp(&r, &p, sizeof(SceneParams)) == 0, "off leaves the params alone");
    const geode::viz::MacroKnobs user{0.1f, -0.2f, 0.0f, 0.3f, 0.0f, 0.5f};
    check(off.knobs(user) == user, "and the knobs");
}

// The look is an offset on the user's dials, so a slider at either end of its range must not be
// pushed past it: a saturation below zero inverts the colour.
void testDialsStayInRange() {
    Director d;
    d.configure({1.0f, 99});
    float t = 0.0f;
    run(d, kSad, 15.0f, t);
    SceneParams low;
    low.saturation = 0.0f;
    low.brightness = 0.2f;
    low.contrast = 0.3f;
    low.temperature = -1.0f;
    low.hueRange = 0.0f;
    low.paletteMix = low.milkdropPaletteTint = low.camSmooth = 0.0f;
    low.motionBreath = low.motionDrift = 0.0f;
    SceneParams high;
    high.saturation = 1.5f;
    high.brightness = 2.0f;
    high.contrast = 1.25f;
    high.temperature = 1.0f;
    high.hueRange = 1.5f;
    high.paletteMix = high.milkdropPaletteTint = high.camSmooth = 1.0f;
    high.motionBreath = high.motionDrift = 1.0f;
    for (const SceneParams& user : {low, high}) {
        const SceneParams o = d.apply(user);
        const auto in = [](float v, float lo, float hi) { return v >= lo && v <= hi; };
        check(in(o.saturation, 0.0f, 1.5f) && in(o.brightness, 0.2f, 2.0f) && in(o.contrast, 0.3f, 1.25f) &&
                  in(o.temperature, -1.0f, 1.0f) && in(o.hueRange, 0.0f, 1.5f),
              "the look never pushes a colour dial past its range");
        check(in(o.paletteMix, 0.0f, 1.0f) && in(o.milkdropPaletteTint, 0.0f, 1.0f) && in(o.camSmooth, 0.0f, 1.0f) &&
                  in(o.motionBreath, 0.0f, 1.0f) && in(o.motionDrift, 0.0f, 1.0f),
              "nor a unit dial");
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
    const float hue = d.look().hue;
    check(weight > 0.9f, "the look is fully in after fifteen seconds");
    d.configure({1.0f, 6});
    check(d.commits() == 0, "a new seed still starts the decisions over");
    d.step(frame(kSad, t, kDt), kDt);
    t += kDt;
    check(d.weight() >= weight - 1e-3f && std::fabs(d.look().hue - hue) < 1e-4f, "the look stays on screen into the next track");
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
}  // namespace

int main() {
    testTable();
    testLiveFeeling();
    testPacing();
    testWeakEventsDecide();
    testNeverTheSame();
    testFeelingSurvivesTheSeed();
    testDeterministicAndOff();
    testNoStyleMomentWhenOff();
    testDialsStayInRange();
    testWeakDownbeatsCount();
    testLookCarriesAcrossPlays();
    testLongStretchStepsTheStyle();
    if (failures) {
        std::printf("director_test: %d failed\n", failures);
        return 1;
    }
    std::printf("director_test: all passed\n");
    return 0;
}
