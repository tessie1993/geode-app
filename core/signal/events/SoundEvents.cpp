#include "signal/events/SoundEvents.hpp"

#include <algorithm>
#include <cmath>

namespace geode::signal {

namespace {

// Events that are one sound-frame trigger each. To add one: a row in EventKinds.inc, a row here.
struct TriggerRow {
    EventKind kind;
    float GeodeSoundFrame::*field;
};

constexpr TriggerRow kTriggers[] = {
    {EventKind::Kick, &GeodeSoundFrame::kick},
    {EventKind::Snare, &GeodeSoundFrame::snare},
    {EventKind::Hat, &GeodeSoundFrame::hat},
    {EventKind::Onset, &GeodeSoundFrame::onset},
    {EventKind::Beat, &GeodeSoundFrame::beat},
    {EventKind::Downbeat, &GeodeSoundFrame::downbeat},
    {EventKind::Phrase, &GeodeSoundFrame::phrase},
    {EventKind::HardCut, &GeodeSoundFrame::hardCut},
    {EventKind::Surge, &GeodeSoundFrame::surge},
    {EventKind::Silence, &GeodeSoundFrame::silence},
    {EventKind::SoundBack, &GeodeSoundFrame::returned},
    {EventKind::NoteChange, &GeodeSoundFrame::noteChange},
    {EventKind::Reattack, &GeodeSoundFrame::reattack},
    {EventKind::ChordChange, &GeodeSoundFrame::harmonicChange},
    {EventKind::Drop, &GeodeSoundFrame::drop},
};

float strengthOf(float v) { return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.0f; }

}  // namespace

int keyOf(const GeodeSoundFrame& f, float minClarity) {
    if (!std::isfinite(f.keyClarity) || f.keyClarity < minClarity || !std::isfinite(f.keyRoot)) return -1;
    if (f.keyMajor > 0.4f && f.keyMajor < 0.6f) return -1;  // undecided between major and minor
    const int root = static_cast<int>(std::lround(f.keyRoot * 12.0f)) % 12;
    const int tonic = root < 0 ? root + 12 : root;
    return f.keyMajor >= 0.6f ? tonic : 12 + tonic;
}

void SoundEvents::detect(const GeodeSoundFrame& f, double timeS, float dt, EventQueue& out) {
    for (const TriggerRow& row : kTriggers) {
        const float v = f.*(row.field);
        if (std::isfinite(v) && v > 0.0f) out.push(Event{row.kind, strengthOf(v), 0, timeS});
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

    // A key change needs the new key clear for kKeyHoldSeconds; a wobble back cancels it. The first
    // clear key after none is raised too: the song's key has been found.
    const int key = keyOf(f, kKeyClarity);
    if (key < 0 || key == key_) {
        candidateKey_ = -1;
        candidateSeconds_ = 0.0f;
    } else {
        if (key != candidateKey_) {
            candidateKey_ = key;
            candidateSeconds_ = 0.0f;
        }
        candidateSeconds_ += std::isfinite(dt) ? std::max(0.0f, dt) : 0.0f;
        if (candidateSeconds_ >= kKeyHoldSeconds) {
            key_ = key;
            candidateKey_ = -1;
            candidateSeconds_ = 0.0f;
            out.push(Event{EventKind::KeyChange, strengthOf(f.keyClarity), key, timeS});
        }
    }
}

void SoundEvents::reset() {
    sectionState_ = -1;
    key_ = -1;
    candidateKey_ = -1;
    candidateSeconds_ = 0.0f;
}

}  // namespace geode::signal
