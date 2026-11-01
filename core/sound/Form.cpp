#include "sound/Form.hpp"

#include <algorithm>
#include <cmath>

#include "sound/Stages.hpp"

namespace geode::sound {

namespace {

constexpr float kArcEvery = 0.25f;
// The arc starts after this much sound: the engine's means, the onset rate and the level peak all climb
// from nothing at first, which reads exactly like a build.
constexpr float kArcWarm = 4.0f;
constexpr float kNoveltyOn = 0.4f;      // novelty a section change must reach ...
constexpr float kNoveltyOverMean = 1.6f; // ... and this many times its own running mean
constexpr int kMinSectionBars = 8;
constexpr int kMinDropBars = 4;         // a drop may cut a section shorter: builds run 4–8 bars
constexpr float kSameSection = 0.3f;    // a returning section sits this close, against how far apart sections are
constexpr float kBuildOn = 0.55f;
constexpr float kBuildOff = 0.25f;
constexpr float kDropAfterBuild = 0.4f;  // the build's peak, in the last 4 s, that arms a drop
// ... and it must have been a build, not a swell: at least 2 s of the last 4 at kBuildOn. A piano cover's
// louder phrase peaks the build reading for a moment and its left hand returns; that is not a drop.
constexpr int kHeldBuildSamples = 8;
constexpr float kDropRefractory = 8.0f;
constexpr float kCountWrap = 4096.0f;

// Least-squares slope of y over its index, per sample.
template <typename F>
float slope(int count, F y) {
    if (count < 2) return 0.0f;
    const float mx = 0.5f * static_cast<float>(count - 1);
    float my = 0.0f;
    for (int i = 0; i < count; i++) my += y(i);
    my /= static_cast<float>(count);
    float num = 0.0f;
    float den = 0.0f;
    for (int i = 0; i < count; i++) {
        const float dx = static_cast<float>(i) - mx;
        num += dx * (y(i) - my);
        den += dx * dx;
    }
    return num / den;
}

}  // namespace

float Form::distance2(const Vec& a, const Vec& b) const {
    // The mean squared difference within each group (the 16 EQ bands, brightness and noisiness, the 12
    // chroma classes), summed: three groups of equal say, however many numbers each holds.
    float eq = 0.0f;
    for (int i = 0; i < GEODE_SOUND_EQ_BANDS; i++) eq += (a[i] - b[i]) * (a[i] - b[i]);
    float timbre = 0.0f;
    for (int i = GEODE_SOUND_EQ_BANDS; i < GEODE_SOUND_EQ_BANDS + 2; i++) timbre += (a[i] - b[i]) * (a[i] - b[i]);
    float chroma = 0.0f;
    for (int i = GEODE_SOUND_EQ_BANDS + 2; i < kDims; i++) chroma += (a[i] - b[i]) * (a[i] - b[i]);
    return eq / GEODE_SOUND_EQ_BANDS + timbre / 2.0f + chroma / 12.0f;
}

void Form::step(const Input& in, const Rhythm::Tick* ticks, int tickCount, float dt, Rhythm& rhythm, GeodeSoundFrame& out) {
    if (!in.silent) {
        heardSeconds_ += dt;
        for (int i = 0; i < GEODE_SOUND_EQ_BANDS; i++) acc_[i] += dt * in.eq[i];
        acc_[GEODE_SOUND_EQ_BANDS] += dt * in.centroid;
        acc_[GEODE_SOUND_EQ_BANDS + 1] += dt * in.flatness;
        for (int i = 0; i < 12; i++) acc_[GEODE_SOUND_EQ_BANDS + 2 + i] += dt * in.chroma[i];
        accWeight_ += dt;
        barLoud_ += dt * in.loudnessDb;
        barBass_ += dt * in.bassShare;
        barWeight_ += dt;

        bassShort_ = follow(bassShort_, in.bassShare, 0.1f, dt);
        bassLong_ = bassLong_ > 0.0f ? follow(bassLong_, in.bassShare, 16.0f, dt) : in.bassShare;
        arcAcc_.loud += dt * in.loudnessDb;
        arcAcc_.centroid += dt * in.centroid;
        arcAcc_.bass += dt * in.bassShare;
        arcWeight_ += dt;
    }
    onsetRate_ = onsetRate_ * std::exp(-dt) + in.events;  // events in the last ~second
    sinceDrop_ += dt;
    sinceArc_ += dt;
    if (sinceArc_ >= kArcEvery) {
        sinceArc_ -= kArcEvery * std::floor(sinceArc_ / kArcEvery);
        if (heardSeconds_ >= kArcWarm) {
            arcSample(dt);
        } else {
            arcAcc_ = {};
            arcWeight_ = 0.0f;
        }
    }
    if (in.silent) buildTarget_ = 0.0f;
    buildUp_ = follow(buildUp_, buildTarget_, 1.0f, dt);

    // The drop: the bass back, well above where it sat for the last two seconds and near its usual
    // share, while a build peaked in the last four (Yadati et al., ISMIR 2014: the bass return is the cue).
    float buildPeak = buildUp_;
    float bassMin = bassShort_;
    int heldBuild = 0;
    for (int i = 0; i < std::min(arcCount_, 16); i++) {
        const ArcSample& a = arc_[(arcHead_ - 1 - i + kArc) % kArc];
        buildPeak = std::max(buildPeak, a.build);
        if (a.build >= kBuildOn) heldBuild++;
        if (i < 8) bassMin = std::min(bassMin, a.bass);
    }
    if (!in.silent && sinceDrop_ >= kDropRefractory && buildPeak >= kDropAfterBuild && heldBuild >= kHeldBuildSamples &&
        arcCount_ >= 8 &&
        bassShort_ > std::max(2.0f * bassMin, 0.6f * bassLong_) && bassShort_ > 0.05f) {
        out.drop = std::clamp(buildPeak, 0.05f, 1.0f);
        sinceDrop_ = 0.0f;
        buildTarget_ = 0.0f;
        buildUp_ = 0.0f;
        for (auto& a : arc_) a.build = 0.0f;
        rhythm.dropHere();
        if (barsSinceChange_ >= kMinDropBars) {
            out.sectionChange = out.drop;
            barsSinceChange_ = 0;
            identifyPending_ = true;
            beatsSinceChange_ = 0;
        }
        changeState(GEODE_SECTION_DROP);
    }

    for (int i = 0; i < tickCount; i++) beat(ticks[i], rhythm, out);

    out.novelty = std::clamp(novelty_ / 0.5f, 0.0f, 1.0f);
    out.sectionId = static_cast<float>(sectionId_);
    out.sectionState = static_cast<float>(state_);
    out.sectionBars = std::fmod(static_cast<float>(stateBars_), kCountWrap);
    out.buildUp = std::clamp(buildUp_, 0.0f, 1.0f);
}

void Form::arcSample(float dt) {
    ArcSample s{};
    if (arcWeight_ > 0.0f) {
        s.loud = arcAcc_.loud / arcWeight_;
        s.centroid = arcAcc_.centroid / arcWeight_;
        s.bass = arcAcc_.bass / arcWeight_;
    } else if (arcCount_ > 0) {
        s = arc_[(arcHead_ - 1 + kArc) % kArc];
        s.loud = -120.0f;
    } else {
        s.loud = -120.0f;
    }
    s.rate = onsetRate_;
    s.build = buildUp_;
    arcAcc_ = {};
    arcWeight_ = 0.0f;
    arc_[arcHead_] = s;
    arcHead_ = (arcHead_ + 1) % kArc;
    arcCount_ = std::min(arcCount_ + 1, kArc);
    (void)dt;
    if (arcCount_ < 8) return;

    // Slopes over the arc, oldest first, each as its change over the full 8 s.
    const int n = arcCount_;
    auto at = [&](int i) -> const ArcSample& { return arc_[(arcHead_ - n + i + 2 * kArc) % kArc]; };
    const float span = static_cast<float>(kArc);
    const float loud = slope(n, [&](int i) { return std::max(at(i).loud, -80.0f); }) * span;
    const float bright = slope(n, [&](int i) { return at(i).centroid; }) * span;
    const float rate = slope(n, [&](int i) { return at(i).rate; }) * span;
    const float bass = slope(n, [&](int i) { return at(i).bass; }) * span;
    loudSlope8s_ = loud;
    float bassNow = 0.0f;
    for (int i = n - 4; i < n; i++) bassNow += 0.25f * at(i).bass;

    // A build: louder, brighter, busier (snare rolls, risers), the bass pulled out. Each cue 0..1 at a
    // typical build's size over 8 s; two or more must agree before it reads as one.
    const float cues[4] = {
        clamp01(loud / 6.0f),
        clamp01(bright / 0.08f),
        clamp01(rate / 4.0f),
        std::max(clamp01(-bass / std::max(0.5f * bassLong_, 1e-3f)), clamp01(1.0f - bassNow / std::max(bassLong_, 1e-3f))),
    };
    const float raw = 0.3f * cues[0] + 0.25f * cues[1] + 0.25f * cues[2] + 0.2f * cues[3];
    int agree = 0;
    for (float c : cues) agree += c > 0.3f ? 1 : 0;
    buildTarget_ = clamp01((agree >= 2 ? raw : 0.5f * raw) * (loud < -1.0f ? 0.5f : 1.0f) * 1.4f);
}

Form::Vec Form::recentBar() const {
    Vec v{};
    const int n = std::min(ringCount_, kAfter);
    for (int i = 0; i < n; i++) {
        const Vec& b = ring_[(ringHead_ - 1 - i + kRing) % kRing];
        for (int d = 0; d < kDims; d++) v[d] += b[d] / static_cast<float>(n);
    }
    return v;
}

float Form::novelty() const {
    // Foote's checkerboard, uniform weights: how alike each half is within itself, minus how alike the
    // halves are to each other. Newest kAfter beats on one side, the kBefore before them on the other.
    if (ringCount_ < kAfter + kBefore || scale_ <= 0.0f) return 0.0f;
    auto slot = [&](int age) { return (ringHead_ - 1 - age + 2 * kRing) % kRing; };
    auto sim = [&](int a, int b) { return std::exp(-d2_[slot(a)][slot(b)] / scale_); };
    float within = 0.0f;
    int withinN = 0;
    float across = 0.0f;
    int acrossN = 0;
    for (int a = 0; a < kAfter + kBefore; a++) {
        for (int b = a + 1; b < kAfter + kBefore; b++) {
            const bool sameSide = (a < kAfter) == (b < kAfter);
            if (sameSide) {
                within += sim(a, b);
                withinN++;
            } else {
                across += sim(a, b);
                acrossN++;
            }
        }
    }
    return within / static_cast<float>(withinN) - across / static_cast<float>(acrossN);
}

int Form::identify(const Vec& bar) {
    // The nearest stored section, if it is near enough; else a new one (the least recently heard is
    // forgotten once eight are stored). "Near" is relative to how far apart sections are: the distance
    // from this bar to the section it just left (novelty says they differ) and between the stored
    // sections. Beat-to-beat spread inside one section is far smaller and would make every return "new".
    int best = -1;
    float bestD2 = 1e30f;
    for (int i = 0; i < sections_; i++) {
        const float d2 = distance2(bar, centroid_[i]);
        if (d2 < bestD2) {
            bestD2 = d2;
            best = i;
        }
    }
    float apart = started_ && sections_ > 0 ? distance2(bar, centroid_[sectionId_]) : 0.0f;
    for (int i = 0; i < sections_; i++) {
        for (int j = i + 1; j < sections_; j++) apart = std::max(apart, distance2(centroid_[i], centroid_[j]));
    }
    if (best >= 0 && best != sectionId_ && bestD2 < kSameSection * apart) return best;
    int slot = sections_;
    if (sections_ < kSections) {
        sections_++;
    } else {
        slot = 0;
        for (int i = 1; i < kSections; i++) {
            if (lastUsed_[i] < lastUsed_[slot]) slot = i;
        }
    }
    centroid_[slot] = bar;
    centroidCount_[slot] = 0;
    return slot;
}

void Form::changeState(int state) {
    if (state == state_) return;
    state_ = state;
    stateBars_ = 0;
}

void Form::classifyAtSectionStart() {
    // A new section, by its first bar: near the loudest the song has been with the bass in → the peak
    // section (a drop, or a chorus); well below it or with the bass gone → a breakdown; else a verse.
    const float rel = lastBarLoud_ - loudRef_;
    const float bass = bassLong_ > 1e-4f ? lastBarBass_ / bassLong_ : 1.0f;
    if (state_ == GEODE_SECTION_BUILD) return;  // a build ends in a drop or by fading, not by novelty
    if (rel >= -2.5f && bass >= 0.8f) {
        changeState(GEODE_SECTION_DROP);
    } else if (rel <= -6.0f || bass < 0.4f) {
        changeState(GEODE_SECTION_BREAKDOWN);
    } else {
        changeState(GEODE_SECTION_VERSE);
    }
}

void Form::beat(const Rhythm::Tick& tick, Rhythm& rhythm, GeodeSoundFrame& out) {
    if (accWeight_ > 0.0f) {
        Vec v{};
        for (int d = 0; d < kDims; d++) v[d] = acc_[d] / accWeight_;
        const int slot = ringHead_;
        ring_[slot] = v;
        float pairs = 0.0f;
        int pairN = 0;
        for (int age = 0; age < ringCount_ && age < kRing - 1; age++) {
            const int other = (slot - 1 - age + kRing) % kRing;
            const float d2 = distance2(v, ring_[other]);
            d2_[slot][other] = d2;
            d2_[other][slot] = d2;
            if (age < kAfter + kBefore) {
                pairs += d2;
                pairN++;
            }
        }
        d2_[slot][slot] = 0.0f;
        ringHead_ = (ringHead_ + 1) % kRing;
        ringCount_ = std::min(ringCount_ + 1, kRing);
        if (pairN > 0) {
            const float typical = pairs / static_cast<float>(pairN);
            scale_ = scale_ > 0.0f ? scale_ + 0.05f * (typical - scale_) : typical;
            scale_ = std::max(scale_, 1e-5f);
        }
        novelty_ = novelty();
        noveltyMean_ += 0.05f * (novelty_ - noveltyMean_);
    }
    acc_ = {};
    accWeight_ = 0.0f;
    beatsSinceChange_++;
    if (!tick.downbeat) return;

    bars_++;
    stateBars_++;
    barsSinceChange_++;
    if (barWeight_ > 0.0f) {
        lastBarLoud_ = barLoud_ / barWeight_;
        lastBarBass_ = barBass_ / barWeight_;
        loudRef_ = std::max(lastBarLoud_, loudRef_ - 0.125f);  // the loudest bar lately, letting go 1 dB per 8 bars
    }
    barLoud_ = barBass_ = barWeight_ = 0.0f;
    if (ringCount_ < kAfter) return;

    const Vec bar = recentBar();
    if (!started_) {
        started_ = true;
        sectionId_ = identify(bar);
    } else if (identifyPending_ && beatsSinceChange_ >= kAfter) {
        identifyPending_ = false;
        sectionId_ = identify(bar);
    } else if (ringCount_ == kRing && barsSinceChange_ > kMinSectionBars && novelty_ / 0.5f > kNoveltyOn && novelty_ > kNoveltyOverMean * noveltyMean_) {
        // The boundary was the last downbeat: the bar just played is the new section's first. Not before
        // the ring holds 32 beats: while the engine's levels settle, the first bars all look new.
        out.sectionChange = std::max(out.sectionChange, std::clamp(novelty_ / 0.5f, 0.05f, 1.0f));
        barsSinceChange_ = 1;
        // Named on the next downbeat, from a bar wholly inside it: this bar may straddle the boundary when
        // the downbeat sits off the true bar start, and would then look like the section just left.
        identifyPending_ = true;
        beatsSinceChange_ = 0;
        rhythm.sectionStarted(1);
        classifyAtSectionStart();
    }
    // The current section learns each bar it plays, slowly, so a returning chorus still matches; not while
    // a new section waits for its name, whose bars are not the old one's.
    if (!identifyPending_) {
        Vec& c = centroid_[sectionId_];
        const int n = std::min(++centroidCount_[sectionId_], 16);
        for (int d = 0; d < kDims; d++) c[d] += (bar[d] - c[d]) / static_cast<float>(n);
        lastUsed_[sectionId_] = bars_;
    }

    // The state between section starts.
    const float rel = lastBarLoud_ - loudRef_;
    const float bass = bassLong_ > 1e-4f ? lastBarBass_ / bassLong_ : 1.0f;
    quietBars_ = rel < -6.0f ? quietBars_ + 1 : 0;
    if (buildUp_ >= kBuildOn && state_ != GEODE_SECTION_BUILD && (state_ != GEODE_SECTION_DROP || stateBars_ >= 4)) {
        changeState(GEODE_SECTION_BUILD);
    } else if (state_ == GEODE_SECTION_BUILD && buildUp_ < kBuildOff && stateBars_ >= 2) {
        changeState(rel > -4.0f && bass > 0.7f ? GEODE_SECTION_VERSE : GEODE_SECTION_BREAKDOWN);
    } else if (state_ == GEODE_SECTION_INTRO && bars_ >= 16) {
        classifyAtSectionStart();
    } else if (state_ == GEODE_SECTION_DROP && stateBars_ >= 2 && (rel < -6.0f || bass < 0.4f)) {
        changeState(GEODE_SECTION_BREAKDOWN);
    } else if (state_ != GEODE_SECTION_INTRO && state_ != GEODE_SECTION_OUTRO && quietBars_ >= 4 && loudSlope8s_ < -4.0f) {
        changeState(GEODE_SECTION_OUTRO);
    } else if (state_ == GEODE_SECTION_OUTRO && rel > -4.0f) {
        changeState(GEODE_SECTION_VERSE);
    }
}

}  // namespace geode::sound
