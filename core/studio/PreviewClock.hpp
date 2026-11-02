#pragma once

#include <cstdint>
#include <vector>

namespace geode::studio {

/*
 * The Studio preview's programme clock (wave 1, part 2B).
 *
 * The preview plays a project live: the song lane through a player, the visual lanes drawn by the
 * GL renderer, footage, stills and text over them. Everything shown follows one time, the
 * programme time this clock keeps. It runs on the wall clock (so it keeps going through a gap in
 * the song, or a project with no song at all) and is pulled toward the audio player's position
 * whenever the player reports one, so the picture stays on the beat it is drawn to without jumping:
 *
 * - a small error is slewed out a fraction per report, which a viewer never sees as a skip;
 * - a large one (a seek the player made, a stall, a resumed app) snaps, since slewing it would
 *   leave the visual seconds off the music.
 *
 * It also answers the two lookups the preview makes every frame: which audio span is under a
 * programme time (and how far into it), and the frame a time falls on at the project's frame rate,
 * so a paused, scrubbed preview always shows the frame the render will have there.
 *
 * Times are programme milliseconds; "now" is a monotonic nanosecond clock from the caller.
 */
class PreviewClock {
public:
    struct Span {
        int64_t startMs;
        int64_t durationMs;
    };

    /** The audio lane's clips, in programme time, in start order. */
    void setSpans(std::vector<Span> spans);

    /** Frame rate for [frameOf], and the programme length the clock stops at (0 = no end). */
    void setRate(int fps, int64_t lengthMs);

    void play(int64_t programmeMs, int64_t nowNs);
    void pause(int64_t nowNs);
    void seek(int64_t programmeMs, int64_t nowNs);

    /** The audio player is at [spanPosMs] into span [span] at [nowNs]. */
    void audio(int span, int64_t spanPosMs, int64_t nowNs);

    /** Programme time at [nowNs], on a frame boundary, clamped to the length. */
    int64_t now(int64_t nowNs) const;

    bool playing() const { return playing_; }

    /** Whether the clock reached the programme's end. */
    bool ended(int64_t nowNs) const;

    /** The span under [programmeMs] and the offset into it, or -1 in a gap. */
    int locate(int64_t programmeMs, int64_t* offsetMs) const;

    /** The start of the frame [programmeMs] falls on at [fps]. */
    static int64_t frameOf(int64_t programmeMs, int fps);

    /** Errors below this are slewed; above it they snap. */
    static constexpr int64_t kSnapMs = 250;
    /** Share of a small error taken out per audio report. */
    static constexpr double kSlew = 0.15;

private:
    double rawMs(int64_t nowNs) const;
    void anchor(double programmeMs, int64_t nowNs);

    std::vector<Span> spans_;
    int fps_ = 30;
    int64_t lengthMs_ = 0;
    bool playing_ = false;
    double anchorMs_ = 0.0;
    int64_t anchorNs_ = 0;
};

}  // namespace geode::studio
