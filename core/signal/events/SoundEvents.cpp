#include "signal/events/SoundEvents.hpp"

#include <algorithm>
#include <cmath>

namespace geode::signal {

namespace {

// Events read from one sound-frame trigger each, some also counted by a frame counter (see the header).
// To add one: a row in EventKinds.inc, a row here; a counted one also gets the next counter slot.
struct TriggerRow {
    EventKind kind;
    float GeodeSoundFrame::*trigger;
    float GeodeSoundFrame::*counter;  // nullptr: trigger-driven
    int counterSlot;                  // index into SoundEvents::lastCount_, -1 without a counter
};

constexpr TriggerRow kTriggers[] = {
    {EventKind::Kick, &GeodeSoundFrame::kick, nullptr, -1},
    {EventKind::Snare, &GeodeSoundFrame::snare, nullptr, -1},
    {EventKind::Hat, &GeodeSoundFrame::hat, nullptr, -1},
    {EventKind::Onset, &GeodeSoundFrame::onset, nullptr, -1},
    {EventKind::Beat, &GeodeSoundFrame::beat, &GeodeSoundFrame::beatCount, 0},
    {EventKind::Downbeat, &GeodeSoundFrame::downbeat, &GeodeSoundFrame::barCount, 1},
    {EventKind::Phrase, &GeodeSoundFrame::phrase, nullptr, -1},
    {EventKind::HardCut, &GeodeSoundFrame::hardCut, nullptr, -1},
    {EventKind::Surge, &GeodeSoundFrame::surge, nullptr, -1},
    {EventKind::Silence, &GeodeSoundFrame::silence, nullptr, -1},
    {EventKind::SoundBack, &GeodeSoundFrame::returned, nullptr, -1},
    {EventKind::NoteChange, &GeodeSoundFrame::noteChange, nullptr, -1},
    {EventKind::Reattack, &GeodeSoundFrame::reattack, nullptr, -1},
    {EventKind::ChordChange, &GeodeSoundFrame::harmonicChange, nullptr, -1},
    {EventKind::Drop, &GeodeSoundFrame::drop, nullptr, -1},
};

// The frame's event counters wrap here (core/sound/Rhythm's kCountWrap).
constexpr std::int64_t kCountWrap = 4096;
// stepsOf's answers besides a step count.
constexpr int kUnknown = -1;  // no base yet, or a non-finite counter: the trigger alone decides
constexpr int kResync = -2;   // the counter went backwards: raise nothing, take the new base

float strengthOf(float v) { return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.0f; }

// How far a counter moved since the last frame, capped at kMaxBurst. A step back is a resync, except
// across the wrap (4095 → 2 is three steps); a non-finite counter keeps the old base.
int stepsOf(std::int64_t& last, float raw) {
    if (!std::isfinite(raw)) return kUnknown;
    const std::int64_t now = ((static_cast<std::int64_t>(std::llround(raw)) % kCountWrap) + kCountWrap) % kCountWrap;
    const std::int64_t before = last;
    last = now;
    if (before < 0) return kUnknown;
    std::int64_t d = now - before;
    if (d < 0) {
        if (d + kCountWrap > SoundEvents::kMaxBurst) return kResync;
        d += kCountWrap;
    }
    return static_cast<int>(std::min<std::int64_t>(d, SoundEvents::kMaxBurst));
}

int pitchClassOf(float unit) {
    const int pc = static_cast<int>(std::lround(unit * 12.0f) % 12);
    return pc < 0 ? pc + 12 : pc;
}

}  // namespace

int keyOf(const GeodeSoundFrame& f) {
    if (!std::isfinite(f.keyRoot) || !std::isfinite(f.keyHue)) return -1;
    const int tonic = pitchClassOf(f.keyRoot);
    const int hue = pitchClassOf(f.keyHue);  // fifths position, 0..11
    if (hue == (tonic * 7) % 12) return tonic;                // major: the tonic's own place
    if (hue == ((tonic + 3) * 7) % 12) return 12 + tonic;     // minor: its relative major's
    return -1;
}

void SoundEvents::detect(const GeodeSoundFrame& f, double timeS, float /*dt*/, EventQueue& out) {
    for (const TriggerRow& row : kTriggers) {
        const float v = f.*(row.trigger);
        const bool fired = std::isfinite(v) && v > 0.0f;
        int n = fired ? 1 : 0;
        if (row.counter != nullptr) {
            const int steps = stepsOf(lastCount_[row.counterSlot], f.*(row.counter));
            if (steps == kResync) {
                n = 0;
            } else if (steps >= 0 && fired) {
                n = std::max(steps, 1);  // a trigger with no step (a frame that left the count alone) is one
            }
        }
        for (int i = 0; i < n; ++i) out.push(Event{row.kind, strengthOf(v), 0, timeS});
    }

    if (std::isfinite(f.sectionChange) && f.sectionChange > 0.0f) {
        const int id = std::isfinite(f.sectionId) ? static_cast<int>(std::lround(f.sectionId)) : 0;
        out.push(Event{EventKind::SectionChange, strengthOf(f.sectionChange), id, timeS});
    }

    // A build starts and ends with the section state, not with buildUp's level, so it fires once each.
    const int state = std::isfinite(f.sectionState) ? static_cast<int>(std::lround(f.sectionState)) : GEODE_SECTION_INTRO;
    if (sectionState_ >= 0 && state != sectionState_) {
        if (state == GEODE_SECTION_BUILD) out.push(Event{EventKind::BuildStart, 1.0f, state, timeS});
        if (sectionState_ == GEODE_SECTION_BUILD) out.push(Event{EventKind::BuildEnd, 1.0f, state, timeS});
    }
    sectionState_ = state;

    // A key change is the frame's key changing value. Harmony already holds a new key two seconds
    // before it takes over, so there is no clarity or hold rule here. The first frame only seeds it; a
    // frame that names no key leaves the last one standing.
    const int key = keyOf(f);
    if (key >= 0) {
        if (key_ >= 0 && key != key_) out.push(Event{EventKind::KeyChange, strengthOf(f.keyClarity), key, timeS});
        key_ = key;
    }
}

void SoundEvents::reset() {
    for (std::int64_t& c : lastCount_) c = -1;
    sectionState_ = -1;
    key_ = -1;
}

}  // namespace geode::signal
