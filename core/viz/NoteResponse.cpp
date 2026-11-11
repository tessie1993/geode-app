#include "viz/NoteResponse.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <initializer_list>

#include "sound/DriveGain.hpp"

namespace geode::viz {

namespace {

std::atomic<bool> gEnabled{false};

constexpr float kTau = 6.2831853f;

// Both NaN-safe: a garbage frame (a NaN from a driver or a bad file) reads as 0, never poisons a filter.
float clamp01(float x) { return x > 0.0f ? (x < 1.0f ? x : 1.0f) : 0.0f; }
// One-pole toward `target`, rising with time constant `up` and falling with `down` (seconds).
float ease(float x, float target, float up, float down, float dt) {
    const float tau = target > x ? up : down;
    return x + (target - x) * (1.0f - std::exp(-dt / std::max(tau, 1e-3f)));
}
float finite(float x, float fallback = 0.0f) { return std::isfinite(x) ? x : fallback; }

// splitmix64, the house RNG (Director, ReactionDna): one value per (seed, stream, index).
float draw(uint64_t seed, uint64_t stream, uint64_t index) {
    uint64_t z = seed ^ (stream * 0xD1B54A32D192ED03ull) ^ (index * 0x9E3779B97F4A7C15ull);
    z += 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return static_cast<float>(z >> 40) / 16777216.0f;
}

constexpr uint64_t kStreamChord = 0xC40Dull;

float tuning(float x) { return std::clamp(finite(x, 1.0f), 0.0f, 2.0f); }

constexpr float kLeapOctaves = 5.0f / 12.0f;  // a fourth or more is a leap
constexpr float kChordShiftMinSeconds = 1.5f;

}  // namespace

namespace note_response {
void setGlobal(bool enabled) { gEnabled.store(enabled, std::memory_order_relaxed); }
bool globalEnabled() { return gEnabled.load(std::memory_order_relaxed); }
}  // namespace note_response

void NoteResponse::reset() {
    const NoteResponseConfig keep = config_;
    const bool configured = configured_;
    *this = NoteResponse();
    config_ = keep;
    configured_ = configured;
}

void NoteResponse::configure(const NoteResponseConfig& config) {
    // A new seed is a new play: forget the song so far, as the director does.
    if (configured_ && config.seed != config_.seed) {
        const float amount = amount_;
        reset();
        amount_ = amount;
    }
    config_ = config;
    configured_ = true;
}

void NoteResponse::step(const GeodeSoundFrame& f, const sound::Emotion::State& e, const Inputs& in, float dt) {
    if (!(dt > 0.0f)) return;
    amount_ += ((config_.enabled ? 1.0f : 0.0f) - amount_) * (1.0f - std::exp(-dt / 0.7f));
    if (!config_.enabled && amount_ < 1e-3f) amount_ = 0.0f;
    if (amount_ <= 0.0f) {
        // Off: every knob at exactly 0.5. Forget everything, so switching it on starts from the music
        // then, not from what it heard minutes ago.
        reset();
        return;
    }
    in_ = in;
    for (float* t : {&in_.pitch, &in_.intervals, &in_.tension, &in_.chords, &in_.drums}) *t = tuning(*t);
    measurePercussiveness(f, dt);
    stepChords(f, dt);
    stepIntervals(f, dt);
    stepKnobs(f, e, dt);
    publish();
}

void NoteResponse::measurePercussiveness(const GeodeSoundFrame& f, float dt) {
    // Rates as leaky counters over ~3 s: each event adds 1/τ, so the counter settles at events per second.
    constexpr float kRateTau = 3.0f;
    const float decay = std::exp(-dt / kRateTau);
    const float hits = (f.kick > 0.0f ? 1.0f : 0.0f) + (f.snare > 0.0f ? 1.0f : 0.0f) + (f.hat > 0.0f ? 1.0f : 0.0f);
    hitRate_ = hitRate_ * decay + hits / kRateTau;
    onsetRate_ = onsetRate_ * decay + (f.onset > 0.0f ? 1.0f : 0.0f) / kRateTau;
    // Slow means of what tells drums from tones: noisiness and spiky high-frequency content against a
    // confident pitch, a clear key and a scale that fits.
    flatness_ = ease(flatness_, clamp01(f.flatness), 2.0f, 2.0f, dt);
    hfc_ = ease(hfc_, clamp01((f.hfc - 1.0f) / 4.0f) * 4.0f, 2.0f, 2.0f, dt);
    pitchConf_ = ease(pitchConf_, clamp01(f.pitchConfidence), 2.0f, 2.0f, dt);
    keyClarity_ = ease(keyClarity_, clamp01(f.keyClarity), 2.0f, 2.0f, dt);
    scaleFit_ = ease(scaleFit_, clamp01(f.scaleFit), 2.0f, 2.0f, dt);
    const float drums = 0.4f * clamp01(hitRate_ / 4.0f) + 0.3f * clamp01(onsetRate_ / 5.0f) + 0.15f * clamp01(flatness_ * 2.0f) +
                        0.15f * clamp01(hfc_ * 1.5f);
    const float tones = 0.45f * pitchConf_ + 0.3f * keyClarity_ + 0.25f * scaleFit_;
    const float target = std::clamp(0.5f + 0.9f * (drums - tones), 0.05f, 0.95f);
    perc_ = finite(ease(perc_, target, 3.0f, 3.0f, dt), 0.5f);
}

void NoteResponse::stepChords(const GeodeSoundFrame& f, float dt) {
    // A chord change turns the swing spring, at most once per kChordShiftMinSeconds.
    sinceShift_ += dt;
    if (f.harmonicChange > 0.0f && sinceShift_ >= kChordShiftMinSeconds && in_.chords > 0.05f) {
        sinceShift_ = 0.0f;
        ++shifts_;
        swingVel_ += (draw(config_.seed, kStreamChord + 2, static_cast<uint64_t>(shifts_)) - 0.5f) * 1.6f * std::min(in_.chords, 1.5f);
    }
}

void NoteResponse::stepIntervals(const GeodeSoundFrame& f, float dt) {
    if (f.noteChange > 0.0f) {
        const float iv = std::clamp(finite(f.interval), -1.0f, 1.0f);
        const float size = std::fabs(iv);
        if (size >= kLeapOctaves - 1e-3f) {
            // A leap swings: a kick to a damped spring, harder the wider the leap, in its direction.
            swingVel_ += in_.intervals * (iv > 0.0f ? 1.0f : -1.0f) * (1.0f + 1.5f * std::min(size, 1.0f));
        } else {
            // A step glides: it adds to a slow, leaky drift in the step's direction.
            glide_ += in_.intervals * iv * 1.5f;
        }
    }
    // Spring at 0.45 Hz, damping 0.35: a leap's swing rings once or twice and settles in about two seconds.
    constexpr float kOmega = kTau * 0.45f;
    constexpr float kZeta = 0.35f;
    swingVel_ += (-kOmega * kOmega * swing_ - 2.0f * kZeta * kOmega * swingVel_) * dt;
    swing_ = std::clamp(swing_ + swingVel_ * dt, -1.0f, 1.0f);
    glide_ = std::clamp(glide_ * std::exp(-dt / 1.5f), -1.0f, 1.0f);
    swing_ = finite(swing_);
    swingVel_ = finite(swingVel_);
    glide_ = finite(glide_);
}

void NoteResponse::stepKnobs(const GeodeSoundFrame& f, const sound::Emotion::State& e, float dt) {
    // k0 PULSE: the drums, weighted by how percussive the track is. A drum-led track pumps the knob on
    // every hit; on a ballad the same soft onsets barely move it.
    const float drumEnv = std::max({f.kickEnv, 0.8f * f.snareEnv, 0.5f * f.hatEnv});
    gain_ = sound::driveGain(f);
    drum_ = std::min(perc_ * in_.drums, 1.0f) * clamp01(0.75f * drumEnv + 0.25f * f.onsetEnv) * gain_;
    const float pulse = clamp01(0.15f + 0.85f * drum_ + 0.2f * perc_ * clamp01(f.bassAtt - 1.0f));
    // k1 TEXTURE: timbre: brightness (centroid, treble), how many partials sound, how noisy they are.
    const float texture = clamp01(0.35f * clamp01((f.centroid - 0.25f) / 0.5f) + 0.25f * clamp01(f.densityFast) +
                                  0.2f * clamp01(f.flatness * 1.5f) + 0.2f * clamp01(f.trebleAtt * 0.5f));
    // k2 FLOW: sustained energy: the song's intensity, the level, the short-over-long loudness trend.
    const float flow = clamp01(0.45f * clamp01(f.intensity) + 0.3f * clamp01(f.level) + 0.25f * clamp01(f.energyTrend));
    // k3 TONE: the melody's height, trusted as far as the pitch is, with the strongest partial for colour.
    if (f.pitch > 0.0f && f.pitchConfidence >= 0.3f) tone_ = ease(tone_, clamp01(f.pitchHeight), 0.12f, 0.12f, dt);
    const float trust = clamp01(pitchConf_ * 1.6f);
    const float tone = clamp01(0.5f + in_.pitch * ((tone_ - 0.5f) * trust + 0.15f * (clamp01(f.peakFreq) - 0.5f)));
    // k4 BUILD: where the music is heading: the feeling's tension and energy, a build, the chord's dissonance.
    const float build = clamp01(0.15f + 0.35f * clamp01(e.energy) +
                                in_.tension * (0.45f * clamp01(e.tension) + 0.35f * clamp01(f.buildUp) + 0.2f * clamp01(f.tension)));
    // k5 SPACE: how far the music moves: interval swing and glide, and the stereo field's width.
    const float space = clamp01(0.32f + 0.6f * clamp01(f.stereoWidth) + 0.35f * std::fabs(swing_) + 0.3f * std::fabs(glide_));
    knobs_[0] = ease(knobs_[0], pulse, 0.02f, 0.18f, dt);
    knobs_[1] = ease(knobs_[1], texture, 0.25f, 0.5f, dt);
    knobs_[2] = ease(knobs_[2], flow, 0.8f, 1.2f, dt);
    knobs_[3] = ease(knobs_[3], tone, 0.1f, 0.25f, dt);
    knobs_[4] = ease(knobs_[4], build, 0.6f, 1.5f, dt);
    knobs_[5] = ease(knobs_[5], space, 0.15f, 0.6f, dt);
    for (float& k : knobs_) k = clamp01(finite(k, 0.5f));
}

void NoteResponse::publish() {
    out_.amount = amount_;
    // The knobs that say how much (pulse, flow, build, space) spend the music's absolute reach; texture and tone
    // are what the sound is, not how loud, and stay as they are.
    for (int i = 0; i < NoteUniforms::kKnobs; ++i) {
        const float reach = (i == 1 || i == 3) ? 1.0f : gain_;
        out_.knobs[i] = 0.5f + (knobs_[i] - 0.5f) * amount_ * reach;
    }
}

}  // namespace geode::viz
