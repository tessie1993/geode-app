#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace geode::sound {

/*
 * Which ring sample the speaker is playing now (#349).
 *
 * The tap sees each sample when the sink takes it, not when it is heard. Between the two sit the
 * AudioTrack's buffer (Media3 sizes it at about 250 ms for PCM, up to 750 ms), the HAL, and on
 * Bluetooth the A2DP link (another 150-250 ms). A renderer that hears up to the ring's newest sample
 * draws every beat, onset and drop that much early. This clock answers the one question a renderer
 * needs: the newest ring sample the listener has heard at a given monotonic time, so its cursor stops
 * there.
 *
 * It measures nothing itself; the sink already does. Two streams of facts come in, both in the sink's
 * media time (the renderer timeline Media3 stamps on every buffer, continuous across a gapless track
 * change, reset by a seek):
 *
 *   - anchors: "the buffer presented at media time P begins at ring sample R and put F samples in the
 *     ring". The tap stays first in the chain (owner, #482), ahead of SilenceSkipping and Sonic, so the
 *     ring holds source samples and one media second is `rate` ring samples: between two anchors the
 *     map is ringStart + (media - mediaStart) * rate. It is taken from consecutive real anchors rather
 *     than from one epoch start and a rate so that a format change mid-stream (gapless into a track at
 *     another rate) needs no epoch of its own: each stretch carries the rate its own buffers had.
 *   - the sink's position: AudioSink.getCurrentPositionUs, in media time, read at a monotonic time.
 *     Media3 takes it from AudioTrack.getTimestamp, which already includes the HAL and Bluetooth
 *     latency, and already counts speed and skipped silence: at 1.5x it runs 1.5 media seconds per wall
 *     second, and a gap SilenceSkipping cut shows as a jump. Both map straight through the anchors,
 *     because the cut samples are in the ring (the tap heard them) and simply never reach the speaker.
 *     Between reports the position moves on at `speed` media seconds per wall second while playing,
 *     and stands while paused.
 *
 * So speakerFrame(now) = anchors(position(now - offset)), clamped to the anchored samples and never
 * past the newest sample written. The A/V offset is wall time (what the user lines up by eye): a
 * positive offset shows the picture later, as if the speaker's latency were that much longer.
 *
 * It is a pure function of the facts handed in and `now`: nothing a read does changes what the next
 * read returns. That is what lets every surface sample it at its own frame rate (30, 60, 120 fps, an
 * export's 1/fps) and get the same sample at the same time.
 *
 * Worked example, 48 kHz, 10 ms buffers: after a seek to 10 s the sink fills its 250 ms AudioTrack
 * buffer at decode speed, anchoring (10 000 000 us, ring 0) ... (10 240 000 us, ring 11 520). The
 * position still reads 10 000 000, so the speaker is at ring 0: the engine hears none of the burst
 * yet. One second into playback the anchors reach ring 59 520 + 480 written, the position reads
 * 11 000 000, the speaker is at ring 48 000, and the engine trails the tap by the 12 000 samples still
 * in the buffer, which is exactly what the listener has not heard.
 *
 * Threads: the player's playback thread writes (bufferHandled, position, playing, flush, speed), any
 * thread sets the offset, every renderer's GL thread reads speakerFrame. One small mutex held for a
 * binary search at most; nobody waits on it for long.
 * Units: media microseconds, monotonic nanoseconds (System.nanoTime), absolute ring sample indices
 * (the ring's running write count, per channel frame, never reset).
 */
class PlayoutClock {
public:
    /** Past this since the last report the sink has stopped reporting (a stall); hold, don't run on. */
    static constexpr int64_t kMaxExtrapolateNs = 250'000'000;

    /** The A/V offset's range. A second either way covers every headset that misreports its latency. */
    static constexpr int kMaxOffsetMs = 1000;

    /**
     * Anchors kept. At Opus's smallest 2.5 ms buffers that is 2.5 s of history, at the usual 10-85 ms
     * buffers 10 s or more: always more than the sink's buffer plus a Bluetooth link plus the offset.
     */
    static constexpr int kAnchors = 1024;

    /** speakerFrame's answer when there is no map at all (nothing handled since create or flush). */
    static constexpr int64_t kUnknown = -1;

    /**
     * The sink handled a buffer presented at [presentationUs] whose first sample is ring sample
     * [ringFrame] and which put [frames] samples in the ring. A repeat of the newest anchor's time (the
     * sink retrying a buffer it could not take whole) is not a new anchor. Time or ring position going
     * backwards without a flush is a discontinuity: the map starts again from this anchor.
     */
    void bufferHandled(int64_t presentationUs, int64_t ringFrame, int64_t frames);

    /** The sink's playback position [positionUs] in media time, read at [nowNs]. */
    void position(int64_t positionUs, int64_t nowNs);

    /** The sink started (true) or paused (false) at [nowNs]; the position only moves on while playing. */
    void playing(bool on, int64_t nowNs);

    /** The sink was flushed (a seek, a new item after a stop): every anchor and position is stale. */
    void flush();

    /** Playback speed, media time per wall time. Ignored unless finite and positive. */
    void speed(float speed);

    /** The user's A/V offset in ms, clamped to +-kMaxOffsetMs; positive shows the picture later. */
    void setOffsetMs(int ms);

    /**
     * The newest ring sample the listener has heard at monotonic [nowNs], offset applied: never past
     * the newest sample written, never before the oldest anchor kept. kUnknown when nothing has been
     * handled since create or the last flush; the oldest anchor's sample while no position has been
     * read (nothing of it has reached the speaker yet).
     */
    int64_t speakerFrame(int64_t nowNs) const;

private:
    struct Anchor {
        int64_t presentationUs;
        int64_t ringFrame;
        int64_t frames;
    };

    // Media time the speaker plays at monotonic tNs, from the last report and the play span around it.
    double positionAt(int64_t tNs, int64_t nowNs) const;
    // Ring sample for media time us through the anchors (count_ > 0).
    int64_t ringAt(double us) const;
    const Anchor& anchor(int i) const { return anchors_[static_cast<std::size_t>((first_ + i) % kAnchors)]; }

    mutable std::mutex mutex_;

    std::array<Anchor, kAnchors> anchors_{};
    int first_ = 0;  // index of the oldest anchor in anchors_
    int count_ = 0;
    int64_t newestWritten_ = 0;  // one past the newest ring sample any anchor put in the ring

    bool positioned_ = false;  // a position was read since create or the last flush
    int64_t positionUs_ = 0;
    int64_t positionNs_ = 0;
    bool playing_ = false;
    // The current (or, paused, the last) play span in monotonic time. Reading behind the last report
    // (a positive offset) runs the position backwards along it, never past its start: before it the
    // speaker stood still (paused) or played audio this map no longer knows (before a flush).
    int64_t playStartNs_ = 0;
    int64_t playEndNs_ = 0;  // when paused: the pause time
    double speed_ = 1.0;
    int64_t offsetNs_ = 0;
};

}  // namespace geode::sound
