#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace geode::viz {

// One look a schedule switches to: a style, and the MilkDrop preset it loads ("" for none).
struct StyleCue {
    double atMs = 0.0;
    std::string sceneId;
    std::string milkPath;
};

// Which style an export draws, frame by frame, for one rendered segment (Studio 2D, ADR 0003).
//
// Two sources, either or both:
// - timed cues: a recorded take's style switches, at render time (ms since the segment began),
//   so a take renders exactly as performed instead of on the style it started on;
// - a beat cut: a list of styles stepped every `everyBeats` beats, counted from the renderer's own
//   sound engine (GeodeSoundFrame::beatCount), so the cut lands on the song's beat in the export
//   exactly as it would live.
//
// beatCount keeps counting at the last tempo heard while the tracker is unsure (the beat trigger
// stays silent then). Before any tempo is heard there are no beats to count, and the cut waits.
//
// The entitlement (#357): `lockedIds` are the catalogue styles this user may not draw (empty for Pro
// and the trial). set() drops every cue and beat-cut id among them, so a take recorded during the
// trial, or while a Pro style was previewed, renders its free styles and holds the look before each
// locked switch instead of drawing it. The segment's opening style is gated by the caller before the
// render starts; this is every later style, gated where the schedule is built, on exactly the ids
// that would be drawn.
//
// It runs on the picture's clock, like every other move (owner's rule: no sound, no movement; #999 F3):
// the renderer calls advance() with the frame's step and the music's rate (sound::motionRate), so in a
// silent stretch the schedule holds with the picture. The timed cues' clock stops, and the beats the
// tracker's flywheel keeps ticking through the silence are not counted: a cut never lands on a frozen
// frame, and the first cut after the silence comes on the music's own beats.
//
// Pure: no GL, no locks. The renderer owns one, steps it once per frame from beginFrame and
// applies whatever change it returns through its own setScene / loadMilkPreset.
class StyleSchedule {
public:
    // Replaces the schedule. Cues need not be sorted. everyBeats below 1 counts as 1. Cues and cut ids
    // naming a style in `lockedIds` are dropped.
    void set(std::vector<StyleCue> cues, std::vector<std::string> beatCutIds, int everyBeats,
             const std::vector<std::string>& lockedIds = {});
    void clear();
    bool empty() const { return cues_.empty() && cut_.empty(); }

    // The look to switch to this frame, or nullptr when it stays. `timeMs` is render time since the
    // segment began; `beatCount` this frame's GeodeSoundFrame::beatCount. The first call only
    // remembers the beat count and applies the cue in force at `timeMs`, if it differs from none.
    const StyleCue* step(double timeMs, float beatCount);

    // The renderer's step: step() at the schedule's own clock (music ms since the segment began), then
    // that clock moves on by frameStepS times motionRate. A still frame (motionRate 0, or not finite)
    // returns nullptr, moves no clock and only remembers beatCount, so no beat of the silence counts.
    const StyleCue* advance(float frameStepS, float motionRate, float beatCount);
    // The schedule's clock: music time since the segment began, in ms.
    double clockMs() const { return clockMs_; }

    // Beats between two beatCount readings, across the 4096 wrap.
    static int beatsBetween(float before, float after);

private:
    const StyleCue* change(const StyleCue& cue);

    std::vector<StyleCue> cues_;
    std::size_t nextCue_ = 0;
    std::vector<std::string> cut_;
    int everyBeats_ = 4;
    std::size_t cutIndex_ = 0;
    int beatsSinceCut_ = 0;
    bool started_ = false;
    float lastBeatCount_ = 0.0f;
    double clockMs_ = 0.0;
    StyleCue current_;
    StyleCue pending_;
};

} // namespace geode::viz
