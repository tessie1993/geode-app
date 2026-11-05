#pragma once

#include <cstdint>
#include <mutex>

namespace geode::audio {

/*
 * Which frame of the PCM ring the speaker is playing now (#349).
 *
 * The tap sits first in the player's sink chain, so the ring gets each sample when the sink writes
 * it into the AudioTrack, not when it is heard. Between the two sit the AudioTrack's buffer (Media3
 * sizes it at about 250 ms for PCM, up to 750 ms), the HAL, and on Bluetooth the A2DP link: a
 * renderer that hears up to the ring's newest frame draws every beat, onset and drop 0.3 s early on
 * the speaker and 0.5 s early on a headset. This clock says how far behind the newest frame the
 * speaker is, so each renderer's cursor stops there.
 *
 * It does not measure the delay itself; the sink already does. Media3's sink reports its playback
 * position in media time (from AudioTrack.getTimestamp, which includes the HAL and Bluetooth
 * latency, else the play head minus AudioTrack's latency), and counts it the way the ring counts
 * frames: from the first buffer handed to it after a flush or a format change, one media frame per
 * ring frame, whatever the speed (the tap is ahead of Sonic) or the encoder delay trimmed (ahead of
 * the tap). So with the media time of an epoch's first frame, a position maps to a ring frame:
 *
 *     heard = (position + time since that report * speed while playing - epochStart) * rate
 *
 * clamped to [0, written]. Worked example: seek to 10 s at 48 kHz. The epoch starts at 10 000 000 us;
 * the sink fills its 250 ms buffer (written 12 000) before the track plays, the position still reads
 * 10 000 000, so the engine hears nothing yet instead of the whole burst. One second into playback
 * written is 60 000, the position 11 000 000, heard 48 000: the engine trails the tap by the 12 000
 * frames still in the buffer, which is what the listener has not heard yet.
 *
 * An epoch the player did not begin (the mic, another app's audio) has no sink in front of it and is
 * heard as written. An epoch the player began but has not yet timed (no first buffer or no position
 * since) holds at 0: nothing of it has reached the speaker yet.
 *
 * Threads: the player's playback thread writes (beginEpoch, firstBuffer, position, playing, speed),
 * every renderer's GL thread reads heardFrame. One small mutex; nobody waits on it for long.
 * Times: media microseconds, and the caller's monotonic nanoseconds (System.nanoTime).
 */
class PlayoutClock {
public:
    /** Extrapolate a position at most this far: past it the player has stopped reporting (stalled). */
    static constexpr int64_t kMaxExtrapolateNs = 250'000'000;

    /** The most the speaker can trail the tap; a bigger lag is a bad mapping and is clamped to it. */
    static constexpr int64_t kMaxLagUs = 2'000'000;

    /** Forget everything (a new playback session). */
    void reset();

    /** The player's tap began ring epoch [epoch] at [rateHz]; its first buffer's time comes next. */
    void beginEpoch(int epoch, int rateHz);

    /**
     * The sink handled (some of) a buffer starting at media time [mediaUs]. The first one after
     * [beginEpoch] is the epoch's frame 0; later ones are ignored.
     */
    void bufferHandled(int64_t mediaUs);

    /** The sink was flushed (a seek): positions from before it no longer apply. */
    void flush();

    /** The sink's playback position, in media time, read at [nowNs]. */
    void position(int64_t mediaUs, int64_t nowNs);

    /** The sink started or paused; the position only moves on while playing. */
    void playing(bool on, int64_t nowNs);

    /** Playback speed (media time per wall time). */
    void speed(float speed);

    /**
     * The newest frame of ring epoch [epoch] the listener has heard at [nowNs], given the ring holds
     * [written] frames of it: never above [written], never below 0. A cursor hands its engine frames
     * up to this one and no further.
     */
    int64_t heardFrame(int epoch, int64_t written, int64_t nowNs) const;

private:
    mutable std::mutex mutex_;
    int epoch_ = -1;            // the ring epoch the player's tap began last; -1: none
    int rateHz_ = 0;
    bool started_ = false;      // epoch_'s first buffer time is known
    int64_t startUs_ = 0;       // media time of epoch_'s frame 0
    bool positioned_ = false;   // a position was read since the epoch began or the sink flushed
    int64_t positionUs_ = 0;
    int64_t positionNs_ = 0;
    bool playing_ = false;
    double speed_ = 1.0;
};

}  // namespace geode::audio
