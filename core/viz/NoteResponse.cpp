#include "viz/NoteResponse.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <initializer_list>

#include "sound/Form.hpp"
#include "viz/Circular.hpp"

namespace geode::viz {

namespace {

std::atomic<bool> gEnabled{false};

constexpr float kTau = 6.2831853f;

// Both NaN-safe: a garbage frame (a NaN from a driver or a bad file) reads as 0, never poisons a filter.
float clamp01(float x) { return x > 0.0f ? (x < 1.0f ? x : 1.0f) : 0.0f; }
// The colour wheel's one set of helpers (viz/Circular.hpp): every hue here eases the short way round.
using circular::mixHue;
using circular::wrap01;
float smooth(float x) {
    const float t = clamp01(x);
    return t * t * (3.0f - 2.0f * t);
}
// One-pole toward `target`, rising with time constant `up` and falling with `down` (seconds).
float ease(float x, float target, float up, float down, float dt) {
    const float tau = target > x ? up : down;
    return x + (target - x) * (1.0f - std::exp(-dt / std::max(tau, 1e-3f)));
}
float finite(float x, float fallback = 0.0f) { return std::isfinite(x) ? x : fallback; }

// splitmix64, the house RNG (Director, ReactionDna, macros::surprise): one value per (seed, stream, index).
float draw(uint64_t seed, uint64_t stream, uint64_t index) {
    uint64_t z = seed ^ (stream * 0xD1B54A32D192ED03ull) ^ (index * 0x9E3779B97F4A7C15ull);
    z += 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return static_cast<float>(z >> 40) / 16777216.0f;
}

constexpr uint64_t kStreamBloom = 0xB100Bull;
constexpr uint64_t kStreamChord = 0xC40Dull;
constexpr uint64_t kStreamMood = 0x300Dull;

float lerp(float a, float b, float t) { return a + (b - a) * t; }
float tuning(float x) { return std::clamp(finite(x, 1.0f), 0.0f, 2.0f); }

// Each look's unfurl time and size against the flower's (lib_bloom): a glow blooms at once and small, a
// ripple spreads slowly and wide like a ring on water, a burst throws its sparks out fast and far.
struct LookShape {
    float openSeconds;
    float radius;
};
LookShape lookShape(int look) {
    switch (look) {
        case 2: return {0.25f, 0.8f};
        case 3: return {1.4f, 1.6f};
        case 4: return {0.4f, 1.3f};
        default: return {0.7f, 1.0f};
    }
}

// A pitch class against the key's tonic, on the circle of fifths, -0.5 .. 0.5: the tonic is 0, the
// dominant +1/12, the subdominant -1/12, the notes outside the key past ±5/12. Neighbours in harmony are
// neighbours in colour, so a diatonic melody spreads over half the wheel and a chromatic one over all of it.
float fifthsFromTonic(float pitchClass, float keyRoot) {
    const int pc = static_cast<int>(std::lround(clamp01(pitchClass) * 12.0f)) % 12;
    const int root = static_cast<int>(std::lround(clamp01(keyRoot) * 12.0f)) % 12;
    const int steps = ((pc - root) % 12 + 12) % 12;
    const int fifths = (steps * 7) % 12;
    const float pos = static_cast<float>(fifths) / 12.0f;
    return pos >= 0.5f ? pos - 1.0f : pos;
}

// How wide the notes spread around the anchor hue: one fifth is 0.85/12 of the wheel.
constexpr float kHueSpread = 0.85f;
constexpr float kLeapOctaves = 5.0f / 12.0f;  // a fourth or more is a leap
constexpr float kChordShiftMinSeconds = 1.5f;

}  // namespace

namespace note_response {
void setGlobal(bool enabled) { gEnabled.store(enabled, std::memory_order_relaxed); }
bool globalEnabled() { return gEnabled.load(std::memory_order_relaxed); }
}  // namespace note_response

float NoteResponse::Bloom::envelope() const {
    if (!alive) return 0.0f;
    // Never a flash: the bloom takes 160 ms to reach full brightness (WCAG counts a flash as a rise and
    // fall within a third of a second; a rise this soft over a patch this small is a glow, not a flash).
    const float grow = smooth(age / 0.16f);
    const float fade = release < 0.0f ? 1.0f : 1.0f - smooth(release / std::max(releaseSeconds, 0.05f));
    return strength * grow * fade;
}

float NoteResponse::Bloom::open() const { return smooth(age / std::max(openSeconds, 0.05f)); }

void NoteResponse::reset() {
    const NoteResponseConfig keep = config_;
    const bool configured = configured_;
    *this = NoteResponse();
    config_ = keep;
    configured_ = configured;
}

void NoteResponse::configure(const NoteResponseConfig& config) {
    // A new seed is a new play: forget the blooms and the song so far, as the director does.
    if (configured_ && config.seed != config_.seed) {
        const float amount = amount_;
        reset();
        amount_ = amount;
    }
    config_ = config;
    configured_ = true;
}

int NoteResponse::liveBlooms() const {
    int n = 0;
    for (const Bloom& b : blooms_) n += b.alive ? 1 : 0;
    return n;
}

void NoteResponse::step(const GeodeSoundFrame& f, const sound::Emotion::State& e, const Inputs& in, float dt) {
    if (!(dt > 0.0f)) return;
    amount_ += ((config_.enabled ? 1.0f : 0.0f) - amount_) * (1.0f - std::exp(-dt / 0.7f));
    if (!config_.enabled && amount_ < 1e-3f) amount_ = 0.0f;
    if (amount_ <= 0.0f) {
        // Off is exactly today's picture: knobs at 0.5, no blooms, apply() a no-op. Forget everything, so
        // switching it on starts from the music then, not from what it heard minutes ago.
        reset();
        return;
    }
    // The director's anchor, when it drives, glides the notes' colours into its look.
    anchorIn_ = finite(in.anchorHue);
    anchorWeight_ = clamp01(finite(in.anchorWeight));
    in_ = in;
    for (float* t : {&in_.color, &in_.pitch, &in_.intervals, &in_.tension, &in_.chords, &in_.drums, &in_.bloom}) *t = tuning(*t);
    in_.look = in.look >= 1 && in.look <= 4 ? in.look : 1;
    for (float& c : in_.camFlat) c = finite(c);
    if (!(in_.camFlat[2] > 0.0f)) in_.camFlat[2] = 1.0f;
    stepPersonality(f, dt);
    measurePercussiveness(f, dt);
    stepHarmony(f, e, dt);
    stepIntervals(f, dt);
    stepKnobs(f, e, dt);
    stepBlooms(f, e, dt);
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

void NoteResponse::stepHarmony(const GeodeSoundFrame& f, const sound::Emotion::State& e, float dt) {
    keyRoot_ = f.keyClarity >= 0.15f ? f.keyRoot : keyRoot_;
    keyWarmth_ = ease(keyWarmth_, (2.0f * clamp01(f.keyMajor) - 1.0f) * clamp01(f.keyClarity), 1.5f, 1.5f, dt);
    valence_ = ease(valence_, std::clamp(finite(e.valence), -1.0f, 1.0f), 1.0f, 1.0f, dt);
    // The key's palette family: the key on the circle of fifths, pulled toward amber when it is major and
    // toward deep blue when minor (Palmer et al. 2013: major and happy music is matched to lighter, warmer,
    // yellower colours; minor and sad to darker, bluer ones). Under the director its look's hue leads.
    const float keyBase = f.keyClarity >= 0.15f ? wrap01(f.keyHue) : 0.76f;
    const float mood = keyWarmth_ >= 0.0f ? 0.07f : 0.62f;
    const float keyAnchor = mixHue(keyBase, mood, 0.55f * std::fabs(keyWarmth_));
    const float target = mixHue(keyAnchor, wrap01(anchorIn_), anchorWeight_);
    anchor_ = primed_ ? mixHue(anchor_, target, 1.0f - std::exp(-dt / 1.2f)) : target;
    primed_ = true;
    // Tension bends, resolution smooths back: quick to strain (a quarter second), slower to relax.
    const float strain = clamp01(in_.tension * (0.6f * clamp01(f.tension) + 0.4f * clamp01(e.tension) - 0.2f) / 0.6f);
    distortion_ = ease(distortion_, strain, 0.25f, 0.9f, dt);
    // A build pushes the camera in, slowly; the drop lets it go quickly.
    build_ = ease(build_, clamp01(f.buildUp), 1.5f, 0.6f, dt);
    // A chord change: a scene shift. The fold count steps by one or two (held until the next change), the
    // bloom layout turns, and the picture gets a turn of its own through the swing spring.
    sinceShift_ += dt;
    if (f.harmonicChange > 0.0f && sinceShift_ >= kChordShiftMinSeconds && in_.chords > 0.05f) {
        sinceShift_ = 0.0f;
        ++shifts_;
        const uint64_t i = static_cast<uint64_t>(shifts_);
        static constexpr int kSteps[4] = {-2, -1, 1, 2};
        const int step = kSteps[std::min(3, static_cast<int>(draw(config_.seed, kStreamChord, i) * 4.0f))];
        int next = symShift_ + step;
        if (next > 3 || next < -3) next = symShift_ - step;
        symShift_ = next;
        layout_ = wrap01(layout_ + 0.25f + 0.5f * draw(config_.seed, kStreamChord + 1, i));
        swingVel_ += (draw(config_.seed, kStreamChord + 2, i) - 0.5f) * 1.6f * std::min(in_.chords, 1.5f);
    }
}

void NoteResponse::stepIntervals(const GeodeSoundFrame& f, float dt) {
    if (f.noteChange > 0.0f) {
        const float iv = std::clamp(finite(f.interval), -1.0f, 1.0f);
        const float size = std::fabs(iv);
        if (size >= kLeapOctaves - 1e-3f) {
            // A leap swings the picture: a kick to a damped spring, harder the wider the leap, in its direction.
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
    drum_ = std::min(perc_ * in_.drums, 1.0f) * clamp01(0.75f * drumEnv + 0.25f * f.onsetEnv);
    const float pulse = clamp01(0.15f + 0.85f * drum_ + 0.2f * perc_ * clamp01(f.bassAtt - 1.0f));
    // k1 TEXTURE: timbre: brightness (centroid, treble), how many partials sound, how noisy they are.
    const float texture = clamp01(0.35f * clamp01((f.centroid - 0.25f) / 0.5f) + 0.25f * clamp01(f.densityFast) +
                                  0.2f * clamp01(f.flatness * 1.5f) + 0.2f * clamp01(f.trebleAtt * 0.5f));
    // k2 FLOW: sustained energy: the song's intensity, the level, the short-over-long loudness trend.
    const float flow = clamp01(0.45f * clamp01(f.intensity) + 0.3f * clamp01(f.level) + 0.25f * clamp01(f.energyTrend));
    // k3 TONE: the melody's height, trusted as far as the pitch is, with the strongest partial for colour.
    if (f.pitch > 0.0f && f.pitchConfidence >= 0.3f) tone_ = ease(tone_, clamp01(f.pitchHeight), 0.12f, 0.12f, dt);
    const float trust = clamp01(pitchConf_ * 1.6f);
    const float tone = clamp01(0.5f + (tone_ - 0.5f) * trust + 0.15f * (clamp01(f.peakFreq) - 0.5f));
    // k4 BUILD: where the music is heading: the feeling's tension and energy, a build, the chord's dissonance.
    const float build = clamp01(0.15f + 0.45f * clamp01(e.tension) + 0.35f * clamp01(f.buildUp) + 0.35f * clamp01(e.energy) +
                                0.2f * clamp01(f.tension));
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

float NoteResponse::noteHue(float pitchClass, float pitchHeight) const {
    // The Note colours amount widens or narrows the spread: 0 paints every note in the anchor's colour.
    const float spread = kHueSpread * std::min(in_.color, 1.6f);
    return wrap01(anchor_ + spread * fifthsFromTonic(pitchClass, keyRoot_) + 0.06f * (clamp01(pitchHeight) - 0.5f));
}

void NoteResponse::roll() {
    // A new character for the play or the section: each trait its own draw, so they vary independently.
    const uint64_t i = static_cast<uint64_t>(rolls_);
    auto r = [&](uint64_t k) { return draw(config_.seed, kStreamMood + k, i); };
    moodTarget_.maxAlive = std::floor(lerp(3.0f, 8.99f, r(0)));
    moodTarget_.budget = lerp(1.5f, 3.0f, r(1));
    moodTarget_.size = lerp(0.75f, 1.35f, r(2));
    // Squared, so most rolls tint lightly and a few lean in hard.
    moodTarget_.tint = lerp(0.04f, 0.3f, r(3) * r(3));
    moodTarget_.travel = lerp(0.15f, 1.0f, r(4));
    moodTarget_.tilt = lerp(0.0f, 0.9f, r(5));
    if (rolls_ == 0) mood_ = moodTarget_;
    ++rolls_;
}

void NoteResponse::stepPersonality(const GeodeSoundFrame& f, float dt) {
    if (rolls_ == 0 || sound::sectionChanged(f) || sound::dropped(f)) roll();
    // The new character eases in over a couple of seconds.
    const float k = 1.0f - std::exp(-dt / 2.5f);
    mood_.maxAlive += (moodTarget_.maxAlive - mood_.maxAlive) * k;
    mood_.budget += (moodTarget_.budget - mood_.budget) * k;
    mood_.size += (moodTarget_.size - mood_.size) * k;
    mood_.tint += (moodTarget_.tint - mood_.tint) * k;
    mood_.travel += (moodTarget_.travel - mood_.travel) * k;
    mood_.tilt += (moodTarget_.tilt - mood_.tilt) * k;
}

void NoteResponse::spawn(const GeodeSoundFrame& f, const sound::Emotion::State& e) {
    // The note before lets go: its bloom fades while the new one grows.
    for (Bloom& b : blooms_) {
        if (b.alive && b.release < 0.0f) b.release = 0.0f;
    }
    // How many may be on screen is the play's character (3..8); past it, the faintest gives its place.
    const int cap = std::clamp(static_cast<int>(std::lround(mood_.maxAlive)), 1, NoteUniforms::kBlooms);
    int slot = -1;
    if (liveBlooms() < cap) {
        for (int i = 0; i < NoteUniforms::kBlooms; ++i) {
            if (!blooms_[i].alive) {
                slot = i;
                break;
            }
        }
    }
    if (slot < 0) {
        // Full: the faintest live one (the oldest on a tie) gives its place.
        for (int i = 0; i < NoteUniforms::kBlooms; ++i) {
            if (!blooms_[i].alive) continue;
            if (slot < 0) {
                slot = i;
                continue;
            }
            const float a = blooms_[i].envelope();
            const float b = blooms_[slot].envelope();
            if (a < b || (a == b && blooms_[i].index < blooms_[slot].index)) slot = i;
        }
        if (slot < 0) slot = 0;
    }
    const uint64_t n = static_cast<uint64_t>(spawned_);
    auto r = [&](uint64_t k) { return draw(config_.seed, kStreamBloom + k, n); };
    const float height = clamp01(f.pitchHeight);
    const float fifths = fifthsFromTonic(f.pitchClass, keyRoot_);
    const float tensionMix = clamp01(0.6f * clamp01(f.tension) + 0.4f * clamp01(e.tension));

    Bloom b;
    b.alive = true;
    b.index = spawned_;
    b.note = static_cast<int>(std::lround(f.pitch));
    b.hue = wrap01(noteHue(f.pitchClass, height) + 0.03f * (r(0) - 0.5f));
    // Where: across by harmony (the circle of fifths around the key's own centre, turned by the chord
    // layout), up by pitch: a melody climbing the scale climbs the picture. A major scale's notes sit at
    // -1..+5 fifths from the tonic and a natural minor's at -4..+2, so each is centred on its middle and
    // spread over the width.
    const float centre = f.keyMajor >= 0.5f ? 2.0f / 12.0f : -1.0f / 12.0f;
    const float across = wrap01(0.5f + 1.4f * (fifths - centre) + layout_);
    b.x = std::clamp(0.1f + 0.8f * across + 0.06f * (r(1) - 0.5f), 0.06f, 0.94f);
    b.y = std::clamp(0.5f + 1.1f * std::min(in_.pitch, 1.5f) * (height - 0.5f) + 0.08f * (r(2) - 0.5f), 0.08f, 0.92f);
    // How bright: the note's attack and the mood, inside the flash budget: past the play's budget (1.5 to
    // three blooms a second, never more than WCAG's three flashes) each new one is dimmer, by the square
    // root of the excess, so a fast run reads as a shimmer, never as strobing.
    float strength = clamp01(0.55f + 0.45f * std::max({f.onsetEnv, f.noteChange, f.reattack})) * (0.85f + 0.15f * valence_);
    recentSpawns_ += 1.0f;
    const float budget = std::clamp(mood_.budget, 1.0f, 3.0f);
    if (recentSpawns_ > budget) strength *= std::sqrt(budget / recentSpawns_);
    b.strength = clamp01(strength);
    // Shape: high notes open and stretch up, low ones squat and pull in; the look and the play set the size.
    const LookShape shape = lookShape(in_.look);
    const float pitchK = std::min(in_.pitch, 1.6f);
    b.radius = (0.1f + 0.08f * b.strength) * (1.1f - 0.2f * height) * shape.radius * mood_.size;
    b.stretch = std::clamp(1.0f + pitchK * (0.5f * height - 0.2f), 0.8f, 1.3f);
    b.openSeconds = shape.openSeconds;
    // Depth and attitude: near or far in the field, its disc turned toward or away from the viewer.
    b.depth3 = r(6);
    b.tiltX = (r(7) - 0.5f) * 2.0f * mood_.tilt;
    b.tiltY = (r(8) - 0.5f) * 2.0f * mood_.tilt;
    // Chord quality: more tension, more petals and deeper recursion; minor adds a petal; the chord's
    // scene shift moves the fold count of every bloom after it.
    const float minor = f.keyMajor < 0.5f ? 1.0f : 0.0f;
    b.petals = std::clamp(std::floor(5.0f + 4.0f * tensionMix + minor + 1.99f * r(3)) + static_cast<float>(symShift_), 4.0f, 10.0f);
    b.depth = 2.0f + (tensionMix > 0.35f ? 1.0f : 0.0f) + (f.pitchConfidence > 0.6f ? 1.0f : 0.0f);
    b.spin = (r(4) - 0.5f) * 1.2f + (f.interval >= 0.0f ? 0.35f : -0.35f);
    b.seed = r(5);
    b.tension = distortion_;
    // Fast runs make short blooms, held notes long ones.
    b.releaseSeconds = 0.6f + 1.4f * (1.0f - clamp01(f.runDensity));
    blooms_[slot] = b;
    ++spawned_;

    // The note's colour joins the last notes' ring.
    for (int i = NoteUniforms::kHues - 1; i > 0; --i) hues_[i] = hues_[i - 1];
    hues_[0] = b.hue;
    hueCount_ = std::min(hueCount_ + 1, NoteUniforms::kHues);
}

void NoteResponse::stepBlooms(const GeodeSoundFrame& f, const sound::Emotion::State& e, float dt) {
    recentSpawns_ *= std::exp(-dt / 1.0f);
    unvoiced_ = f.pitchConfidence < 0.25f ? unvoiced_ + dt : 0.0f;
    for (Bloom& b : blooms_) {
        if (!b.alive) continue;
        b.age += dt;
        if (b.release >= 0.0f) {
            b.release += dt;
            if (b.release >= b.releaseSeconds) b.alive = false;
        } else if (unvoiced_ > 0.12f || b.age > 4.0f) {
            // The note stopped sounding (or has been held four seconds): the flower lets go.
            b.release = 0.0f;
        }
    }
    const bool onset = (f.noteChange > 0.0f || f.reattack > 0.0f) && f.pitchConfidence >= 0.25f && f.pitch > 0.0f;
    if (onset && config_.enabled) {
        spawn(f, e);
    }
}

void NoteResponse::publish() {
    out_.amount = amount_;
    out_.look = in_.look;
    // The whole picture's tint: a circular mean of the last notes' colours (newer weigh more), smoothed,
    // pulled in as far as the play's character, the Note colours amount and the director allow (the
    // director owns the palette, so under it the tint is halved).
    if (hueCount_ > 0) {
        float sx = 0.0f;
        float sy = 0.0f;
        float w = 1.0f;
        for (int i = 0; i < hueCount_; ++i) {
            sx += w * std::cos(kTau * hues_[i]);
            sy += w * std::sin(kTau * hues_[i]);
            w *= 0.6f;
        }
        const float mean = wrap01(std::atan2(sy, sx) / kTau);
        tintHue_ = tintPrimed_ ? mixHue(tintHue_, mean, 0.05f) : mean;
        tintPrimed_ = true;
    }
    {
        // HSV to RGB at saturation 0.75, value 1: the tint's colour; the composite keeps each pixel's luma.
        const float h = tintHue_ * 6.0f;
        constexpr float kSat = 0.75f;
        auto channel = [&](float n) {
            const float k = std::fmod(n + h, 6.0f);
            return 1.0f - kSat * std::clamp(std::min(k, 4.0f - k), 0.0f, 1.0f);
        };
        const float pull = std::clamp(mood_.tint * std::min(in_.color, 1.5f) * (1.0f - 0.5f * anchorWeight_) * amount_, 0.0f, 0.45f);
        out_.tint = {channel(5.0f), channel(3.0f), channel(1.0f), tintPrimed_ ? pull : 0.0f};
    }
    for (int i = 0; i < NoteUniforms::kKnobs; ++i) out_.knobs[i] = 0.5f + (knobs_[i] - 0.5f) * amount_;
    // Before the first notes the ring is a spread around the anchor already, never one colour.
    for (int i = 0; i < NoteUniforms::kHues; ++i) {
        const float spread = wrap01(anchor_ + kHueSpread * (static_cast<float>(i) / NoteUniforms::kHues - 0.25f));
        out_.hues[i] = i < hueCount_ ? hues_[i] : spread;
    }
    // Live blooms, oldest first so the newest draws on top.
    std::array<int, NoteUniforms::kBlooms> order{};
    int count = 0;
    for (int i = 0; i < NoteUniforms::kBlooms; ++i) {
        if (blooms_[i].alive && blooms_[i].envelope() > 0.002f) order[count++] = i;
    }
    // Eight at most: an insertion sort by spawn order (std::sort trips GCC's array-bounds warning here).
    for (int i = 1; i < count; ++i) {
        const int v = order[i];
        int j = i - 1;
        while (j >= 0 && blooms_[order[j]].index > blooms_[v].index) {
            order[j + 1] = order[j];
            --j;
        }
        order[j + 1] = v;
    }
    out_.bloomCount = count;
    for (int k = 0; k < NoteUniforms::kBlooms; ++k) {
        if (k >= count) {
            out_.bloomA[k] = {};
            out_.bloomB[k] = {};
            out_.bloomC[k] = {};
            out_.bloomD[k] = {};
            continue;
        }
        const Bloom& b = blooms_[order[k]];
        // The flower turns at its own rate and swings with the interval spring.
        const float angle = b.spin * b.age + 0.6f * swing_;
        // Travel and parallax: a near bloom drifts out from the centre and grows as it ages, as if the
        // viewer were moving through a field of them (vection), and slides against the camera's pan, roll
        // and zoom more than a far one does. Far ones are a little dimmer, so the eye reads the depth.
        const float near = 1.0f - b.depth3;
        const float drift = mood_.travel * (0.25f + 0.75f * near) * b.age * 0.12f;
        const auto& cam = in_.camFlat;
        const float zoom = std::pow(cam[2], 0.5f * near) * (1.0f + drift);
        const float roll = -0.5f * near * cam[3];
        const float cr = std::cos(roll);
        const float sr = std::sin(roll);
        const float rx = (b.x - 0.5f) * zoom;
        const float ry = (b.y - 0.5f) * zoom;
        const float x = 0.5f + cr * rx - sr * ry - 0.35f * near * cam[0];
        const float y = 0.5f + sr * rx + cr * ry - 0.35f * near * cam[1];
        const float radius = b.radius * (1.0f + 0.8f * drift) * (1.0f + 0.25f * near);
        const float envelope = std::min(b.envelope() * amount_ * in_.bloom * (1.0f - 0.3f * b.depth3), 1.0f);
        out_.bloomA[k] = {finite(x, 0.5f), finite(y, 0.5f), finite(radius, 0.1f), b.hue};
        out_.bloomB[k] = {b.open(), envelope, b.petals, b.depth};
        out_.bloomC[k] = {angle, b.stretch, b.seed, b.tension};
        // Each disc keeps its own attitude and rocks with the music's swing and glide.
        out_.bloomD[k] = {b.tiltX + 0.25f * swing_, b.tiltY + 0.15f * glide_, b.depth3, 0.0f};
    }
}

SceneParams NoteResponse::apply(const SceneParams& p) const {
    const float a = amount_;
    if (a <= 0.0f) return p;
    SceneParams q = p;
    const float tone = knobs_[3] - 0.5f;
    // Pitch height shapes every style a little: the morph opens for high notes and closes for low ones.
    q.morph = std::clamp(p.morph + a * 0.3f * in_.pitch * tone, 0.0f, 1.0f);
    // Harmonic tension bends: warp and chromatic split (and the blooms' frayed edges, in lib_bloom).
    // Resolution eases them back. Never glitch: a composite glitch on a chord is a flash, not a bend.
    q.warp = std::clamp(p.warp + a * 0.35f * distortion_, 0.0f, 1.0f);
    q.chromaAb = std::clamp(p.chromaAb + a * 0.22f * distortion_, 0.0f, 1.0f);
    // Intervals move it: a leap's swing twists and turns, a run of steps sways.
    q.twist = std::clamp(p.twist + a * 0.35f * swing_, -1.0f, 1.0f);
    q.rotation = std::clamp(p.rotation + a * 0.25f * swing_, -3.0f, 3.0f);
    q.sway = std::clamp(p.sway + a * 0.2f * std::fabs(glide_), 0.0f, 1.0f);
    // Drums, as much as the track is made of them.
    q.zoom = p.zoom * (1.0f + a * 0.035f * drum_);
    // Perspective: a leap rolls and turns the camera, a run of steps leans it, a build pushes it in. Written
    // as camera params, so CameraRig springs toward them and reduced motion and the safety clamp act on top.
    q.camRoll = std::clamp(p.camRoll + a * 0.22f * swing_, -1.0f, 1.0f);
    q.camYaw = std::clamp(p.camYaw + a * (0.12f * glide_ + 0.08f * swing_), -1.0f, 1.0f);
    q.camPush = std::clamp(p.camPush + a * 0.3f * build_, -1.0f, 1.0f);
    // A chord change shifts the fold count; integer, so it lands once the switch is past half.
    if (a >= 0.5f && symShift_ != 0) q.symmetry = std::clamp(p.symmetry + symShift_, 2, 16);
    // Mood, only where the director is not already deciding the palette: major warmer, minor cooler, a
    // happy feeling a touch brighter.
    const float own = a * (1.0f - anchorWeight_);
    q.temperature = std::clamp(p.temperature + own * 0.25f * std::min(in_.color, 1.5f) * keyWarmth_, -1.0f, 1.0f);
    q.brightness = p.brightness * (1.0f + own * 0.06f * valence_);
    return q;
}

}  // namespace geode::viz
