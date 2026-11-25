#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace geode::viz::sceneclock {

// Clock control: the one scale the scene's clocks run at, which responses can drive to 0 or below.
//
// The renderer steps it once a frame with the picture's own step (the frame's step times the music's rate).
//
//   Moves by the signed step (can run back): the picture's clock (uTime and the composite's time), a shader style's
//   own clock, the rate phases (rotation and the other rates) and the dive's phase. uSoundTimeSped follows the scale
//   held at 0 or above: it stops with a freeze or a reverse and never runs back.
//   Moves by the scale, never backwards (rate(), sim()): what the scenes and passes simulate, the camera rig's
//   easing, the liquid's own dive and flow, the shape graph's drift.
//   Never scaled: the audio and its analysis, the responder, the waves' and envelopes' timing, the director, the
//   touch field, the smear and the liquid's gestures, an export's frame times and the frame step itself.
//
// The same steps and the same calls give the same values, so a live frame and an exported one agree.
//
//   Freeze   the scale eases to 0 over its fade in, holds, and eases back over its fade out. With `holdFrame`,
//            while it stands at 0 the renderer may re-present the last composed frame instead of drawing.
//   Reverse  the same envelope toward a negative rate (-1 is the clock run backwards at its own speed).
//   Stutter  the clock loops back over the last L seconds, R times, then carries on where the clock would have
//            been had it never looped (the loop is read from a ring of recent clock values, not frames).
//
// Several at once: the last one fired wins, easing from where the scale is. A Follow response maps a level to a
// scale in [-1, 1] and rules while it is the latest to claim the clock. Pure, header-only, no allocation.
class SceneClock {
public:
    static constexpr float kMaxScale = 4.0f;
    static constexpr float kMaxLoopSeconds = 4.0f;
    // How long a Follow that stops claiming takes to hand the clock back.
    static constexpr float kFollowEaseSeconds = 0.15f;
    static constexpr int kMaxRepeats = 16;
    // Room for kMaxLoopSeconds at 240 fps; a longer ask reaches back as far as the ring goes.
    static constexpr int kRing = 1024;

    // One frame: the scene clock's move for `realDt` seconds of the picture's clock. Negative while the clock runs
    // backwards, and a jump when a stutter loops or resumes. A step that is not positive and finite moves nothing.
    float step(float realDt) {
        if (!(realDt > 0.0f) || !std::isfinite(realDt)) {
            followed_ = false;
            return 0.0f;
        }
        const double before = value_;
        float delta = 0.0f;
        if (stutter_) {
            delta = stepStutter(realDt);
        } else {
            const float was = scale_;
            const bool driven = followed_ && followSeq_ > fireSeq_;
            if (!followed_ && following_) {
                // A Follow that lets go hands the clock back with a short ease, not a snap.
                following_ = false;
                if (phase_ == Phase::Idle && scale_ != 1.0f) {
                    outFrom_ = scale_;
                    out_ = kFollowEaseSeconds;
                    t_ = 0.0f;
                    phase_ = Phase::Out;
                }
            }
            if (driven) {
                scale_ = followScale_;
                holdFrame_ = followHold_;
                phase_ = Phase::Idle;
            } else {
                advance(realDt);
            }
            // At rest the step is the frame's own, bit for bit.
            delta = (was == 1.0f && scale_ == 1.0f) ? realDt : 0.5f * (was + scale_) * realDt;
            value_ += static_cast<double>(delta);
        }
        followed_ = false;
        rate_ = scale_;
        ring_[static_cast<size_t>(head_)] = Sample{before, value_, realDt};
        head_ = (head_ + 1) % kRing;
        count_ = std::min(count_ + 1, kRing);
        return delta;
    }

    // The scale of the last step: 1 at rest, 0 frozen, negative backwards. A simulation scales its own step with
    // sim(): it never runs backwards, and a stutter's jumps are the clocks' own.
    float rate() const { return rate_; }
    float sim(float realDt) const { return realDt * std::max(rate_, 0.0f); }
    // The scene clock's value (unwrapped; it starts at 0 on a reset).
    double value() const { return value_; }
    // Standing at 0 with "hold last frame" on: the picture cannot change, so the renderer need not draw it.
    bool holds() const { return !stutter_ && holdFrame_ && scale_ == 0.0f; }
    bool active() const { return stutter_ || phase_ != Phase::Idle || following_; }

    // Eases to 0 over `easeIn`, stays for `hold` (negative: until release()), eases back over `easeOut`.
    void freeze(float easeIn, float hold, float easeOut, bool holdFrame = false) {
        begin(0.0f, easeIn, hold, easeOut, holdFrame);
    }
    // The same toward -rate (a rate that is not above 0 reads as 1).
    void reverse(float rate, float easeIn, float hold, float easeOut) {
        begin(-(rate > 0.0f && std::isfinite(rate) ? std::min(rate, kMaxScale) : 1.0f), easeIn, hold, easeOut, false);
    }
    // Loops back over the last `loopSeconds` (of the picture's clock), `repeats` times. Nothing to loop (no length,
    // no repeats, a clock that has not moved forward over that time) does nothing and leaves what plays alone.
    void stutter(float loopSeconds, int repeats) {
        if (!(loopSeconds > 0.0f) || !std::isfinite(loopSeconds) || repeats < 1) return;
        const double start = valueAgo(std::min(loopSeconds, kMaxLoopSeconds));
        const double span = value_ - start;
        if (!(span > 1e-6)) return;
        stutter_ = true;
        elapsed_ = 0.0;
        anchor_ = value_;
        loopStart_ = start;
        span_ = span;
        total_ = span * static_cast<double>(std::min(repeats, kMaxRepeats));
        fireSeq_ = ++seq_;
        phase_ = Phase::Idle;
        scale_ = 1.0f;
        holdFrame_ = false;
    }
    // Drive Follow: call every frame the level holds. The scale is 1 at level 0 and `target` at level 1 (0 frozen,
    // negative backwards), held to [-1, 1]. A level of 0 claims nothing.
    void follow(float level, float target, bool holdFrame = false) {
        if (!(level > 0.0f) || !std::isfinite(level) || !std::isfinite(target)) return;
        followScale_ = std::clamp(1.0f + (target - 1.0f) * std::min(level, 1.0f), -1.0f, 1.0f);
        followHold_ = holdFrame;
        if (!following_) {
            following_ = true;
            followSeq_ = ++seq_;
        }
        followed_ = true;
    }
    // Drive While: the event ended, so the hold ends and the scale eases back.
    void release() {
        if (phase_ == Phase::In || phase_ == Phase::Hold) startOut();
    }
    // A new surface or a new scene: the clock starts over at rest.
    void reset() { *this = SceneClock{}; }

private:
    enum class Phase { Idle, In, Hold, Out };
    struct Sample {
        double from = 0.0;
        double to = 0.0;
        float dt = 0.0f;
    };

    static float finiteOr(float v, float fallback) { return std::isfinite(v) ? v : fallback; }
    static float smooth(float x) {
        const float c = std::clamp(x, 0.0f, 1.0f);
        return c * c * (3.0f - 2.0f * c);
    }

    void begin(float target, float easeIn, float hold, float easeOut, bool holdFrame) {
        stutter_ = false;
        fireSeq_ = ++seq_;
        target_ = std::clamp(target, -kMaxScale, kMaxScale);
        from_ = scale_;
        in_ = std::max(finiteOr(easeIn, 0.0f), 0.0f);
        hold_ = std::isfinite(hold) ? hold : 0.0f;
        out_ = std::max(finiteOr(easeOut, 0.0f), 0.0f);
        holdFrame_ = holdFrame;
        phase_ = Phase::In;
        t_ = 0.0f;
    }

    void startOut() {
        outFrom_ = scale_;
        phase_ = Phase::Out;
        t_ = 0.0f;
    }

    // The envelope's phases, carrying what is left of the step into the next so the timing never depends on the rate.
    void advance(float dt) {
        float left = dt;
        for (;;) {
            switch (phase_) {
                case Phase::Idle:
                    scale_ = 1.0f;
                    holdFrame_ = false;
                    return;
                case Phase::In:
                    if (t_ + left < in_) {
                        t_ += left;
                        scale_ = from_ + (target_ - from_) * smooth(t_ / in_);
                        return;
                    }
                    left -= in_ - t_;
                    t_ = 0.0f;
                    scale_ = target_;
                    phase_ = Phase::Hold;
                    break;
                case Phase::Hold:
                    if (hold_ < 0.0f) return;
                    if (t_ + left < hold_) {
                        t_ += left;
                        return;
                    }
                    left -= hold_ - t_;
                    startOut();
                    break;
                case Phase::Out:
                    if (t_ + left < out_) {
                        t_ += left;
                        scale_ = outFrom_ + (1.0f - outFrom_) * smooth(t_ / out_);
                        return;
                    }
                    phase_ = Phase::Idle;
                    break;
            }
        }
    }

    float stepStutter(float realDt) {
        const double before = value_;
        elapsed_ += static_cast<double>(realDt);
        if (elapsed_ < total_) {
            value_ = loopStart_ + std::fmod(elapsed_, span_);
        } else {
            // Where the clock would be had it never looped.
            value_ = anchor_ + elapsed_;
            stutter_ = false;
        }
        scale_ = 1.0f;
        return static_cast<float>(value_ - before);
    }

    // The clock's value `seconds` of the picture's clock ago, read from the ring (the oldest it holds if it reaches
    // back less far).
    double valueAgo(float seconds) const {
        double left = static_cast<double>(seconds);
        double oldest = value_;
        for (int i = 0; i < count_; ++i) {
            const Sample& s = ring_[static_cast<size_t>(((head_ - 1 - i) % kRing + kRing) % kRing)];
            if (left <= static_cast<double>(s.dt)) {
                const double f = s.dt > 0.0f ? left / static_cast<double>(s.dt) : 0.0;
                return s.to - f * (s.to - s.from);
            }
            left -= static_cast<double>(s.dt);
            oldest = s.from;
        }
        return oldest;
    }

    double value_ = 0.0;
    float scale_ = 1.0f;
    float rate_ = 1.0f;
    Phase phase_ = Phase::Idle;
    float target_ = 1.0f;
    float from_ = 1.0f;
    float outFrom_ = 1.0f;
    float in_ = 0.0f;
    float hold_ = 0.0f;
    float out_ = 0.0f;
    float t_ = 0.0f;
    bool holdFrame_ = false;

    bool stutter_ = false;
    double elapsed_ = 0.0;
    double anchor_ = 0.0;
    double loopStart_ = 0.0;
    double span_ = 0.0;
    double total_ = 0.0;

    // Who claimed the clock last: a fire takes the next number, a Follow takes one when it begins.
    unsigned seq_ = 0;
    unsigned fireSeq_ = 0;
    unsigned followSeq_ = 0;
    bool following_ = false;
    bool followed_ = false;
    bool followHold_ = false;
    float followScale_ = 1.0f;

    std::array<Sample, kRing> ring_{};
    int head_ = 0;
    int count_ = 0;
};

}  // namespace geode::viz::sceneclock
