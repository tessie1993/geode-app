#include "audio/dsp/GainStage.hpp"

#include <algorithm>
#include <cmath>

#include "sound/PlayerSignals.hpp"

namespace geode::audio {

void GainStage::Mailbox::post(float db, int ms) {
    std::lock_guard<std::mutex> lock(writer_);
    // Seqlock: odd while writing. The reader drops a block's look when it sees a write in progress or a
    // sequence that moved under it, and finds the command on its next block.
    const uint32_t s = seq_.load(std::memory_order_relaxed);
    seq_.store(s + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    db_.store(db, std::memory_order_relaxed);
    ms_.store(ms, std::memory_order_relaxed);
    seq_.store(s + 2, std::memory_order_release);
}

bool GainStage::Mailbox::take(uint32_t& seen, float& db, int& ms) const {
    const uint32_t s1 = seq_.load(std::memory_order_acquire);
    if (s1 == seen || (s1 & 1u) != 0) return false;
    const float d = db_.load(std::memory_order_relaxed);
    const int m = ms_.load(std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_acquire);
    if (seq_.load(std::memory_order_relaxed) != s1) return false;
    seen = s1;
    db = d;
    ms = m;
    return true;
}

void GainStage::rampTo(float db, int ms) {
    fadeBox_.post(clampDb(db), std::max(0, ms));
}

int64_t GainStage::sleepFade(int64_t remainingMs) {
    std::lock_guard<std::mutex> lock(sleepMutex_);
    if (remainingMs < 0) {
        if (sleepFading_) sleepBox_.post(0.0f, kRecoverMs);
        sleepFading_ = false;
        return 1;
    }
    if (remainingMs > kSleepFadeMs) {
        // A timer restarted longer while it was fading comes back up.
        if (sleepFading_) sleepBox_.post(0.0f, kRecoverMs);
        sleepFading_ = false;
        return remainingMs - kSleepFadeMs;
    }
    // Inside the window: one ramp to silence that ends as the timer does. It is aimed once; the samples
    // keep its time from then on, so later reports need not re-aim it.
    if (!sleepFading_) sleepBox_.post(kSilenceDb, static_cast<int>(remainingMs));
    sleepFading_ = true;
    return std::max<int64_t>(1, remainingMs);
}

void GainStage::setCrossfadeMs(int ms) {
    crossfadeMs_.store(std::max(0, ms), std::memory_order_relaxed);
}

void GainStage::setFollowsOn(bool followsOn) {
    followsOn_.store(followsOn, std::memory_order_relaxed);
}

void GainStage::setSpeed(float speed) {
    speed_.store(std::isfinite(speed) && speed > 0.0f ? speed : 1.0f, std::memory_order_relaxed);
}

void GainStage::beginStream(uint64_t key, int64_t positionUs, int64_t durationUs) {
    const double position = static_cast<double>(std::max<int64_t>(0, positionUs));
    // A new stream, or this one played again from further back (repeat one), against the same stream
    // carrying on after a flush that only re-timed it (a speed change drains, then flushes where it was).
    const bool restarted = key != key_ || position + kRestartSlackUs < mediaUs_;
    if (restarted) {
        // It followed on only if the last stream played out to its end with something after it, the case
        // whose tail the crossfade faded. A skip, a seek or a pick from the queue flushes without an end of
        // stream, and starts at full level, as gapless did.
        followedOn_ = ended_ && endedFollowing_;
        key_ = key;
        replayGainSnap_ = true;
        framesInStream_ = 0;
    }
    // Audio before a flush that did not play out to its end (a seek) was thrown away unheard, so the
    // crossfade lane takes its new level at once instead of sliding from where the old audio left it.
    if (!ended_ || (restarted && !followedOn_)) crossSnap_ = true;
    mediaUs_ = position;
    durationUs_ = durationUs > 0 ? static_cast<double>(durationUs) : 0.0;
    ended_ = false;
}

void GainStage::endStream() {
    ended_ = true;
    endedFollowing_ = followsOn_.load(std::memory_order_relaxed) && crossfadeMs_.load(std::memory_order_relaxed) > 0;
}

void GainStage::refreshReplayGain(int sampleRate) {
    const bool wanted = replayGainSnap_ || replayGain_.generation() != replayGainSeen_;
    if (!wanted) return;
    float db = 0.0f;
    uint64_t generation = 0;
    switch (replayGain_.tryGain(key_, db, generation)) {
        case ReplayGainTable::Lookup::Found:
            // On the stream's first sample the gain is simply its own: the previous stream's last sample is
            // already processed, so there is nothing to ramp from. Once the stream has begun, a new value
            // (late tags, a settings change) glides instead of stepping.
            if (replayGainSnap_ && framesInStream_ == 0) {
                replayGainDb_.jump(db);
            } else if (static_cast<double>(clampDb(db)) != replayGainDb_.target()) {
                replayGainDb_.start(db, msToFrames(kReplayGainGlideMs, sampleRate));
            }
            replayGainSnap_ = false;
            replayGainSeen_ = generation;
            break;
        case ReplayGainTable::Lookup::Unknown:
            // Tags still loading: keep the gain the last stream had, never 0 dB (#351). Their arrival moves
            // the generation, and they glide in.
            replayGainSnap_ = false;
            replayGainSeen_ = generation;
            break;
        case ReplayGainTable::Lookup::Busy:
            break;  // asked again on the next block; past the first sample it glides
    }
}

GainStage::CrossTarget GainStage::crossfadeTarget(double halfUs, bool followsOn, double usPerFrame) const {
    CrossTarget t;
    if (halfUs <= 0.0) return t;
    // Fade-in by the media time before this frame, fade-out by the media time left after it: the first frame
    // of a stream that followed on and the last frame of the one it followed are both silence, and the k-th
    // frame from the join on either side has the same level, so the two halves mirror each other.
    // mediaUs_ is a running sum of frame lengths, so after a few seconds it sits a hair off the frame it
    // stands for. Read raw, the last frame came out a billionth short of silence (and the next stream,
    // finding the tail not silent, held at full level instead of fading in), and a half could gain or lose
    // a frame. Both times are taken to the nearest whole frame, and a half ends a quarter frame early.
    const auto whole = [usPerFrame](double us) { return std::max(0.0, std::round(us / usPerFrame) * usPerFrame); };
    const double edge = halfUs - 0.25 * usPerFrame;
    const double before = whole(mediaUs_);
    if (followedOn_ && before < edge) t.in = kSilenceDb * (1.0 - before / halfUs);
    if (followsOn && durationUs_ > 0.0) {
        const double after = whole(durationUs_ - (mediaUs_ + usPerFrame));
        if (after < edge) t.out = kSilenceDb * (1.0 - after / halfUs);
    }
    return t;
}

void GainStage::process(float* interleaved, size_t frames, int channels, int sampleRate) {
    if (interleaved == nullptr || channels <= 0 || sampleRate <= 0) return;
    float db = 0.0f;
    int ms = 0;
    if (fadeBox_.take(fadeSeen_, db, ms)) fade_.start(db, msToFrames(ms, sampleRate));
    if (sleepBox_.take(sleepSeen_, db, ms)) sleep_.start(db, msToFrames(ms, sampleRate));
    refreshReplayGain(sampleRate);

    const double halfUs = static_cast<double>(crossfadeMs_.load(std::memory_order_relaxed)) * 500.0;
    const bool followsOn = followsOn_.load(std::memory_order_relaxed);
    const double usPerFrame = 1.0e6 * static_cast<double>(speed_.load(std::memory_order_relaxed)) / sampleRate;
    // The crossfade lane never moves faster than a crossfade's own step, so turning the setting off or losing
    // the next item mid-fade slides back instead of jumping; with no crossfade, it recovers over kRecoverMs.
    const double range = -static_cast<double>(kSilenceDb);
    const double crossStep =
        halfUs > 0.0 ? range * usPerFrame / halfUs : range / static_cast<double>(std::max<int64_t>(1, msToFrames(kRecoverMs, sampleRate)));
    const size_t ch = static_cast<size_t>(channels);
    double fadesDb = 0.0;

    for (size_t i = 0; i < frames; ++i) {
        const CrossTarget cross = crossfadeTarget(halfUs, followsOn, usPerFrame);
        const double crossTarget = std::min(cross.in, cross.out);
        if (crossSnap_) {
            crossDb_ = crossTarget;
        } else if (crossTarget < crossDb_ && cross.in < 0.0 && cross.out == 0.0) {
            // A fade-in that starts louder than its line (the tail before it did not reach silence: the
            // setting came on late, or skipped silence shortened it) holds until the line rises to meet it,
            // rather than dipping down to the line first.
        } else if (std::fabs(crossTarget - crossDb_) <= crossStep * (1.0 + 1e-9)) {
            crossDb_ = crossTarget;
        } else {
            crossDb_ += crossTarget > crossDb_ ? crossStep : -crossStep;
        }
        crossSnap_ = false;
        mediaUs_ += usPerFrame;

        const double rg = replayGainDb_.next();
        const double fade = fade_.next();
        const double sleep = sleep_.next();
        // Any lane at the floor silences the frame whatever the others add: a faded-out pause stays silent
        // under a +6 dB ReplayGain.
        const double silence = static_cast<double>(kSilenceDb);
        const bool silent = fade <= silence || sleep <= silence || crossDb_ <= silence;
        const double total = silent ? silence : rg + fade + sleep + crossDb_;
        fadesDb = silent ? silence : fade + sleep + crossDb_;
        if (total != lastDb_) {
            lastDb_ = total;
            lastGain_ = dbToGain(total);
        }
        ++framesInStream_;
        // At exactly 0 dB the samples are left untouched, so a chain with nothing to do changes no bit.
        if (lastGain_ == 1.0f) continue;
        float* frame = interleaved + i * ch;
        for (size_t c = 0; c < ch; ++c) frame[c] *= lastGain_;
    }
    // The engines hear the tap, before these lanes, so they are told the fades instead: the UI, sleep and
    // crossfade lanes as the frame's fadeGain (#460), not ReplayGain, which is a level, not a fade.
    if (frames > 0) sound::player::setFadeGain(dbToGain(fadesDb));
}

}  // namespace geode::audio
