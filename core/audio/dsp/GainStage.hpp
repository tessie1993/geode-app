#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

#include "audio/dsp/GainRamp.hpp"
#include "audio/dsp/ReplayGain.hpp"

namespace geode::audio {

// Every gain the playback applies, as sample-accurate ramps on the samples themselves (#483). Four lanes,
// summed in dB per frame:
//
//   ReplayGain  the stream's tag gain (ReplayGain.hpp), taken on the first sample of the stream it belongs
//               to (#351); tags that arrive late, or a settings change, glide over kReplayGainGlideMs.
//   UI fade     pause, resume and skip: the shell asks for a target and a length (rampTo).
//   Sleep       the sleep timer's last kSleepFadeMs, aimed to reach silence as the timer ends (sleepFade).
//   Crossfade   Settings › Playback › Crossfade: the tail of a stream that something follows fades out over
//               half the setting, and the stream that follows on fades in over the other half. Worked out
//               per frame from the stream's media position, so it is the same with the app open or closed.
//
// This used to be Kotlin coroutines stepping player.volume every 25-40 ms from the main thread: a stair of
// small jumps, timed by the wall clock rather than by the samples, and applied at the AudioTrack, after a
// sink buffer of audio the DSP had already processed. Here every lane is counted in frames of the audio it
// changes, so a ramp starts and ends on the sample it was meant to, and no frame steps by more than one ramp
// increment.
//
// What that costs, until the playout clock (#482) can say which sample the speaker is on: the samples are
// processed up to a sink buffer ahead of the speaker, so a fade the shell asks for now is heard that much
// later, and a shell that pauses after a fade-out waits for the faded audio to drain first.
//
// Threads: the setters may run on any thread and never touch audio-thread state; each posts to a mailbox
// the audio thread reads without blocking. beginStream, endStream and process run on the playback thread.
class GainStage {
public:
    // The sleep timer's fade length; the shell's timer only reports the time left (sleepFade).
    static constexpr int64_t kSleepFadeMs = 3000;
    // A fade cancelled, or a lane brought back without a length of its own: short, so it is not a click.
    static constexpr int kRecoverMs = 50;
    // ReplayGain tags that arrive after their stream began, or a mode or preamp change, glide this long.
    static constexpr int kReplayGainGlideMs = 250;
    // A stream that restarts within this of where it was is the same stream carrying on (a speed change's
    // drain and flush); further back is a new play of it (repeat one).
    static constexpr double kRestartSlackUs = 500000.0;

    // --- any thread ---

    // The UI fade lane: ramp from wherever it is to `db` over `ms`. db <= kSilenceDb (or -inf) is silence.
    void rampTo(float db, int ms);

    // The sleep timer's time left, in ms; < 0 when the timer is cancelled or done. Returns how many ms until
    // the stage next needs to hear it (>= 1), so the timer can sleep until then. Over the last kSleepFadeMs
    // the sleep lane ramps to silence, ending as the timer ends; a cancel brings it back over kRecoverMs.
    int64_t sleepFade(int64_t remainingMs);

    void setCrossfadeMs(int ms);
    // Whether something plays after the current stream on its own (a next item, or repeat one).
    void setFollowsOn(bool followsOn);
    // The playback speed Sonic applies upstream, so a frame here is speed / rate seconds of media.
    void setSpeed(float speed);

    ReplayGainTable& replayGain() { return replayGain_; }

    // --- playback thread ---

    // A stream starts at its next sample (the processor's flush): `key` names its item (streamKey), position
    // is the media position of that sample, duration the item's (<= 0 when unknown).
    void beginStream(uint64_t key, int64_t positionUs, int64_t durationUs);
    // The stream's last sample has been queued (Media3's end of stream, before a gapless hand-off).
    void endStream();

    // In place, RT-safe: no allocation, no blocking lock, no logging.
    void process(float* interleaved, size_t frames, int channels, int sampleRate);

    // The level of the last processed frame, in dB, the four lanes summed (for tests).
    double lastDb() const { return lastDb_; }

private:
    // One ramp command from the shell: a single-writer seqlock, read by the audio thread without blocking.
    class Mailbox {
    public:
        void post(float db, int ms);
        // True with the newest command when one arrived since `seen`.
        bool take(uint32_t& seen, float& db, int& ms) const;

    private:
        std::mutex writer_;
        std::atomic<uint32_t> seq_{0};
        std::atomic<float> db_{0.0f};
        std::atomic<int> ms_{0};
    };

    // The crossfade lane's two lines for the next frame, in dB; 0 where a line does not apply.
    struct CrossTarget {
        double in = 0.0;
        double out = 0.0;
    };

    void refreshReplayGain(int sampleRate);
    CrossTarget crossfadeTarget(double halfUs, bool followsOn, double usPerFrame) const;

    ReplayGainTable replayGain_;
    Mailbox fadeBox_;
    Mailbox sleepBox_;
    std::mutex sleepMutex_;  // sleepFading_; the shell's side only
    bool sleepFading_ = false;
    std::atomic<int> crossfadeMs_{0};
    std::atomic<bool> followsOn_{false};
    std::atomic<float> speed_{1.0f};

    // Playback thread only.
    uint32_t fadeSeen_ = 0;
    uint32_t sleepSeen_ = 0;
    GainRamp replayGainDb_;
    GainRamp fade_;
    GainRamp sleep_;
    double crossDb_ = 0.0;
    uint64_t key_ = 0;
    uint64_t replayGainSeen_ = 0;
    bool replayGainSnap_ = false;
    bool crossSnap_ = false;
    int64_t framesInStream_ = 0;
    double mediaUs_ = 0.0;
    double durationUs_ = 0.0;
    bool ended_ = false;
    bool endedFollowing_ = false;
    bool followedOn_ = false;
    double lastDb_ = 0.0;
    float lastGain_ = 1.0f;
};

}  // namespace geode::audio
