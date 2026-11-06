#pragma once
// Whether a flush of the player's processor chain starts a new epoch on the ingest ring (#358, #487).
//
// Media3 flushes the whole chain, the tap included, for two different reasons, and the tap cannot tell
// them apart from the flush alone:
//   - a sink reset (DefaultAudioSink.flush: a seek, a stop, a reconfigure, an AudioTrack it could not
//     reuse). The audio after it is not the audio before it. Media3 reports no position for it.
//   - a re-apply (DefaultAudioSink.applyAudioProcessorPlaybackParametersAndSkipSilence): a speed, pitch
//     or skip-silence change, the first buffer after a reset, a reused-output item change, a > 200 ms
//     timestamp resync. The chain is drained first and the flush carries the next buffer's media
//     position and period. The samples on either side are contiguous unless the period changed.
// Restarting the sound engine on a re-apply throws away the tempo, key and sections it learned, mid-song,
// on every step of a speed slider. So the Kotlin glue only reports what it saw (re-apply or reset, the
// media position, the period's identity, the format) and this decides: a new epoch only on a real
// discontinuity, otherwise the same epoch with the new format (geode_ingest_set_format).
//
// The tap stays first in the chain (owner, 2026-11-05), so the ring holds source-time samples: every
// frame written advances the media position by exactly 1 / rate, whatever the speed, and skipped silence
// is still written (SilenceSkipping runs after the tap). That is what makes the position check below
// exact. Following what the speaker plays at a speed other than 1x is the playout clock's job (#482,
// #488): it maps the speaker's media position back to a ring position; the ring does not stretch.
//
// Pure and single-threaded: each ring's handle owns one, used on its writer thread (geode_ingest_flush).

#include <cstdint>
#include <cstdlib>

namespace geode::audio::ingest {

// What the glue saw at one flush. Facts only; no decision is made in Kotlin.
struct FlushFacts {
    int sampleRate = 0;
    int channels = 0;
    int channelMask = 0;      // 0 = the default layout for the channel count
    bool reapplied = false;   // the sink re-applied parameters (true) or reset itself (false)
    int64_t positionUs = 0;   // the next buffer's media position; meaningful only when reapplied
    int64_t periodId = 0;     // an identity for the playlist item's period; 0 = unknown
};

enum class FlushVerdict {
    BeginEpoch,   // a discontinuity: readers restart (cursor and sound engine state reset)
    SetFormat,    // the same stream, maybe a new format: readers carry on
};

class FlushJudge {
   public:
    // A re-apply whose position lands further than this from where the written samples put the stream
    // is a real jump. Below it, it is Media3's timestamp resync (it re-applies at > 200 ms of drift) over
    // samples that are still contiguous, which #358 also counts as a wrong reset.
    static constexpr int64_t kJumpUs = 1'000'000;

    // written: the ring's frames written so far (geode_ingest_written), so the judge never counts
    // samples itself and never disagrees with the ring.
    FlushVerdict judge(const FlushFacts& facts, long long written) {
        const FlushVerdict verdict = decide(facts, written);
        // An unknown period keeps the last known one, except across a new epoch, which forgets it.
        if (verdict == FlushVerdict::BeginEpoch || facts.periodId != 0) periodId_ = facts.periodId;
        anchored_ = facts.reapplied;
        anchorUs_ = facts.positionUs;
        anchorWritten_ = written;
        rate_ = facts.sampleRate;
        seen_ = true;
        return verdict;
    }

    // Forget everything: the next flush begins an epoch (a new source owns the ring).
    void reset() { *this = FlushJudge{}; }

   private:
    FlushVerdict decide(const FlushFacts& f, long long written) const {
        if (!seen_) return FlushVerdict::BeginEpoch;
        // A sink reset: a seek or stop. Media3 gives it no position, so nothing proves continuity.
        if (!f.reapplied) return FlushVerdict::BeginEpoch;
        // Another playlist item behind a reused AudioTrack (gapless): a new song, a fresh engine.
        if (f.periodId != 0 && periodId_ != 0 && f.periodId != periodId_) return FlushVerdict::BeginEpoch;
        // The first re-apply after a reset (Media3 re-applies on the first buffer after every reset) or
        // after the first flush: there is no measured anchor yet, and the reset already began the epoch.
        if (!anchored_ || rate_ <= 0) return FlushVerdict::SetFormat;
        const long long since = written - anchorWritten_;
        const int64_t expectedUs = anchorUs_ + static_cast<int64_t>(since * 1'000'000LL / rate_);
        return std::llabs(f.positionUs - expectedUs) > kJumpUs ? FlushVerdict::BeginEpoch : FlushVerdict::SetFormat;
    }

    bool seen_ = false;
    bool anchored_ = false;   // the last flush was a re-apply, so its position is an anchor
    int64_t anchorUs_ = 0;
    long long anchorWritten_ = 0;
    int rate_ = 0;
    int64_t periodId_ = 0;
};

}  // namespace geode::audio::ingest

