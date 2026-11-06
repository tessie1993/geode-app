#pragma once
#include <array>

#include "sound/Rhythm.hpp"
#include "sound/SoundFrame.h"

namespace geode::sound {

// The form half of the musical layer: where in the song we are. Sections, their return, the build and
// the drop, and a coarse section state (intro, verse, build, drop, breakdown, outro).
//
//   hop features ─► mean per beat (the rhythm's ticks) ─► 32-beat ring ─► pairwise distances
//      ─► Foote's checkerboard at the last downbeat but one (16 beats before, 4 after) ─► novelty
//      ─► on a downbeat, 8+ bars after the last: sectionChange ─► nearest stored section ─► sectionId
//   hop features ─► 4 a second, 8 s ring ─► slopes of loudness, brightness, onset rate, bass share
//      ─► buildUp ─► bass back after a build ─► drop
//
// Live means no future: the checkerboard's "after" half is the bar just played, so a section change fires
// one bar after the boundary (on the next downbeat), and the phrase count is told it began a bar ago. A
// drop needs no wait: it fires the moment the bass returns and moves the downbeat there.
class Form {
public:
    // The one floor for Form's two events, drop and sectionChange: the novelty a section change must pass
    // and the build peak that arms a drop. An event fires with its strength (novelty, or the build's peak)
    // and never below this, so a frame either carries one at kEventFloor..1 or holds 0. Consumers act on
    // the event (dropped(), sectionChanged() below), never on a threshold of their own over the strength:
    // a second threshold above this one silently drops the events in between (the director once acted
    // only above 0.5, and missed every drop and section change at 0.4–0.5).
    static constexpr float kEventFloor = 0.4f;

    struct Input {
        std::array<float, GEODE_SOUND_EQ_BANDS> eq;  // each band in dB over the track's peak, 0 (-60 dB) .. 1
        float centroid;
        float flatness;
        const float* chroma;  // the medium chroma, 12, loudest = 1
        float loudnessDb;     // the window's RMS, dBFS
        float bassShare;      // the low band's share of the mix's energy, 0..1
        float events;         // onsets, snares and hats this hop, for the onset rate
        bool silent;
    };

    void step(const Input& in, const Rhythm::Tick* ticks, int tickCount, float dt, Rhythm& rhythm, GeodeSoundFrame& out);
    void reset() { *this = Form(); }

private:
    static constexpr int kDims = GEODE_SOUND_EQ_BANDS + 2 + 12;
    static constexpr int kRing = 32;
    static constexpr int kAfter = 4;    // the checkerboard's newer half: one bar
    static constexpr int kBefore = 16;  // its older half: four bars
    static constexpr int kSections = 8;
    static constexpr int kArc = 32;     // the energy arc's ring, 8 s at 4 a second
    using Vec = std::array<float, kDims>;

    void beat(const Rhythm::Tick& tick, Rhythm& rhythm, GeodeSoundFrame& out);
    float distance2(const Vec& a, const Vec& b) const;
    float novelty() const;
    Vec recentBar() const;
    int identify(const Vec& bar);
    void arcSample(float dt);
    void changeState(int state);
    void classifyAtSectionStart();

    // Per beat.
    Vec acc_{};
    float accWeight_ = 0.0f;
    std::array<Vec, kRing> ring_{};
    std::array<std::array<float, kRing>, kRing> d2_{};  // squared distances between ring slots
    int ringHead_ = 0;  // the next slot to write
    int ringCount_ = 0;
    float scale_ = 0.0f;  // the typical squared distance, so similarity reads the same for any music
    float novelty_ = 0.0f;
    float noveltyMean_ = 0.0f;

    // Sections.
    std::array<Vec, kSections> centroid_{};
    std::array<int, kSections> centroidCount_{};
    std::array<int64_t, kSections> lastUsed_{};
    int sections_ = 0;  // stored so far
    int sectionId_ = 0;
    int barsSinceChange_ = 0;
    bool identifyPending_ = false;
    int beatsSinceChange_ = 0;  // a new section is named once a whole bar of it has played
    int64_t bars_ = 0;
    bool started_ = false;  // the first section's first bar has been stored

    // Per bar, for the state.
    float barLoud_ = 0.0f;
    float barBass_ = 0.0f;
    float barWeight_ = 0.0f;
    float lastBarLoud_ = -60.0f;
    float lastBarBass_ = 0.0f;
    float loudRef_ = -120.0f;

    // The energy arc.
    struct ArcSample {
        float loud;
        float centroid;
        float rate;
        float bass;
        float build;
    };
    std::array<ArcSample, kArc> arc_{};
    int arcHead_ = 0;
    int arcCount_ = 0;
    ArcSample arcAcc_{};
    float arcWeight_ = 0.0f;
    float sinceArc_ = 0.0f;
    float onsetRate_ = 0.0f;
    float bassShort_ = 0.0f;
    float bassLong_ = 0.0f;
    float buildTarget_ = 0.0f;
    float buildUp_ = 0.0f;
    float loudSlope8s_ = 0.0f;  // dB over the arc's 8 s
    float sinceDrop_ = 1e9f;
    float heardSeconds_ = 0.0f;  // sound so far; the arc skips the first seconds, when everything "rises" from nothing

    int state_ = GEODE_SECTION_INTRO;
    int stateBars_ = 0;
    int quietBars_ = 0;
};

// Form fired an event this frame. The strength (f.drop, f.sectionChange) says how big it was, for whoever
// scales by it; whether it happened is this, and only this.
inline bool dropped(const GeodeSoundFrame& f) { return f.drop > 0.0f; }
inline bool sectionChanged(const GeodeSoundFrame& f) { return f.sectionChange > 0.0f; }

}  // namespace geode::sound
