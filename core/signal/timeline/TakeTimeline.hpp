#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// A track take's choices on song time (#457, ADR 0008): the style, MilkDrop preset and every param the
// performer had on screen, each from the song position where it changed, plus the play's seed. A capture take's
// video is what was on screen; this is what lets "Render again" render the same performance offline at any
// quality, through the export's own renderer (output StyleSchedule for the switches, these params per frame).
//
// Recording rules (note()):
// - choices are kept only when something changed, and a choice within kMinGapMs of the last one replaces it, so a
//   slider drag is a handful of choices a second, not one a frame;
// - a song position before the last choice is a seek back, a punch-in: everything recorded from there on goes and
//   recording carries on from the new position;
// - at kMaxChoices the timeline is full and keeps no more (a take that long keeps its video either way).
//
// Replay (stateAt, styleChanges) is the same rules read back. encode()/decode() are the file the take keeps beside
// its project: versioned text, params as changes from the choice before, floats printed exactly.
namespace geode::signal {

struct TakeChoice {
    int64_t atMs = 0;
    std::string sceneId;
    std::string milkPath;
    std::vector<float> params;  // geode_viz_set_params wire order
};

class TakeTimeline {
public:
    static constexpr int64_t kMinGapMs = 50;
    static constexpr size_t kMaxChoices = 6000;
    static constexpr int kVersion = 1;

    // songTime: a track take, its choices on the song's position, so it can render again; false for a take over
    // the mic or another app's audio (#458), its choices on wall time from the take's start.
    explicit TakeTimeline(uint64_t seed = 0, bool songTime = true) : seed_(seed), songTime_(songTime) {}

    void note(int64_t atMs, const std::string& sceneId, const std::string& milkPath, const float* params, int count);

    uint64_t seed() const { return seed_; }
    bool songTime() const { return songTime_; }
    bool full() const { return choices_.size() >= kMaxChoices; }
    bool empty() const { return choices_.empty(); }
    const std::vector<TakeChoice>& choices() const { return choices_; }
    int64_t endMs() const { return choices_.empty() ? 0 : choices_.back().atMs; }

    // "Render again" is offered only for a track take that holds choices: a mic or capture take has no song to
    // render over.
    bool rendersAgain() const { return songTime_ && !choices_.empty(); }

    // The choice in force at song position ms: the last one at or before it, or the first before it begins.
    // Null for an empty timeline.
    const TakeChoice* stateAt(int64_t ms) const;

    // Every choice whose style or preset differs from the one before (the first included): the switches an
    // export's StyleSchedule plays.
    std::vector<const TakeChoice*> styleChanges() const;

    std::string encode() const;
    static std::optional<TakeTimeline> decode(const std::string& text);

private:
    uint64_t seed_;
    bool songTime_;
    std::vector<TakeChoice> choices_;
};

// Render again (#457): a take played back by an export segment's own renderer, frame by frame, with no shell in the
// loop. The renderer holds its own copy of the timeline (taken when the segment is set up, so the shell can close its
// handle whenever it likes) and asks for each frame's choice by render time. A take clip plays its take at speed 1
// from an offset, so render time ms is song time songStartMs + ms. `changed` is false while the same choice stays in
// force, so the renderer pushes params only when the performer changed something.
class TakeReplay {
public:
    struct Frame {
        const TakeChoice* choice = nullptr;  // null for an empty timeline
        bool changed = false;
    };

    TakeReplay(TakeTimeline timeline, int64_t songStartMs) : timeline_(std::move(timeline)), songStartMs_(songStartMs) {}

    // Any render time, in any order: an export renders forward, but a retried segment starts again from 0.
    Frame at(int64_t renderMs);

    uint64_t seed() const { return timeline_.seed(); }
    int64_t songStartMs() const { return songStartMs_; }

private:
    TakeTimeline timeline_;
    int64_t songStartMs_;
    // The last frame's choice by index, not pointer, so a copied or moved replay never compares against another's.
    std::ptrdiff_t last_ = -1;
};

}  // namespace geode::signal
