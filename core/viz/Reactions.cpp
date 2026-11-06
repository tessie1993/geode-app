#include "viz/Reactions.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>

#include "sound/DriveGain.hpp"

namespace geode::viz {

namespace {

// How a GeodeSoundFrame field becomes a route's 0..1.
enum class Kind {
    Relative,  // ~1.0 typical, 2 a strong hit, never past 4: halved, so typical reads 0.5
    Unit,      // already 0..1
    Signed,    // already -1..1
    Clock,     // a clock that only moves forward: its rate per second, times `scale`
    Trigger,   // > 0 on the one frame it fires: a held envelope (instant rise, `scale` seconds fall)
    Scaled,    // a plain number (bpm, an index): times `scale`, clamped to 0..1
};

struct SourceSpec {
    ReactSource source;
    size_t offset;
    Kind kind;
    float scale;
};

#define GEODE_SRC(src, field, kind, scale) SourceSpec{ReactSource::src, offsetof(GeodeSoundFrame, field), Kind::kind, scale}
#define GEODE_EQ(src, band) SourceSpec{ReactSource::src, offsetof(GeodeSoundFrame, eq) + (band) * sizeof(float), Kind::Relative, 0}

// Clock scales: bandTime runs at its band's att (~1/s typical), curvedTime at vol_att^2.5, hitTime at
// hits per second, noteTime at notes per second; each scaled so a busy passage reads near 1.
constexpr std::array<SourceSpec, kReactSourceCount> kSourceTable = {{
    GEODE_SRC(Bass, bass, Relative, 0), GEODE_SRC(Mid, mid, Relative, 0), GEODE_SRC(Treble, treble, Relative, 0),
    GEODE_SRC(Vol, vol, Relative, 0), GEODE_SRC(BassAtt, bassAtt, Relative, 0), GEODE_SRC(MidAtt, midAtt, Relative, 0),
    GEODE_SRC(TrebleAtt, trebleAtt, Relative, 0), GEODE_SRC(VolAtt, volAtt, Relative, 0), GEODE_SRC(Sub, sub, Relative, 0),
    GEODE_SRC(SubAtt, subAtt, Relative, 0), GEODE_SRC(MidHigh, midHigh, Relative, 0), GEODE_SRC(MidHighAtt, midHighAtt, Relative, 0),
    GEODE_SRC(Low, low, Unit, 0), GEODE_SRC(Middle, middle, Unit, 0), GEODE_SRC(High, high, Unit, 0), GEODE_SRC(Level, level, Unit, 0),
    GEODE_SRC(BassPresence, bassPresence, Unit, 0), GEODE_SRC(MidPresence, midPresence, Unit, 0),
    GEODE_SRC(TreblePresence, treblePresence, Unit, 0), GEODE_SRC(Intensity, intensity, Unit, 0), GEODE_SRC(Presence, presence, Unit, 0),
    GEODE_SRC(BassTime, bassTime, Clock, 0.5f), GEODE_SRC(MidTime, midTime, Clock, 0.5f), GEODE_SRC(TrebleTime, trebleTime, Clock, 0.5f),
    GEODE_SRC(CurvedTime, curvedTime, Clock, 0.5f), GEODE_SRC(HitTime, hitTime, Clock, 0.25f), GEODE_SRC(NoteTime, noteTime, Clock, 0.125f),
    GEODE_SRC(Loudness, loudness, Unit, 0), GEODE_SRC(Centroid, centroid, Unit, 0), GEODE_SRC(Rolloff, rolloff, Unit, 0),
    GEODE_SRC(Flatness, flatness, Unit, 0), GEODE_SRC(DensitySlow, densitySlow, Unit, 0), GEODE_SRC(DensityFast, densityFast, Unit, 0),
    GEODE_SRC(Flux, flux, Relative, 0), GEODE_SRC(EnergyTrend, energyTrend, Unit, 0), GEODE_SRC(PeakFreq, peakFreq, Unit, 0),
    GEODE_SRC(PeakMag, peakMag, Unit, 0), GEODE_SRC(StereoWidth, stereoWidth, Unit, 0), GEODE_SRC(StereoPan, stereoPan, Signed, 0),
    GEODE_SRC(KickEnv, kickEnv, Unit, 0), GEODE_SRC(SnareEnv, snareEnv, Unit, 0), GEODE_SRC(HatEnv, hatEnv, Unit, 0),
    GEODE_SRC(OnsetEnv, onsetEnv, Unit, 0), GEODE_SRC(KickToggle, kickToggle, Unit, 0), GEODE_SRC(SnareToggle, snareToggle, Unit, 0),
    GEODE_SRC(KickRandom, kickRandom, Unit, 0),
    GEODE_SRC(Kick, kick, Trigger, 0.25f), GEODE_SRC(Snare, snare, Trigger, 0.25f), GEODE_SRC(Hat, hat, Trigger, 0.15f),
    GEODE_SRC(Kick2, kick2, Trigger, 0.35f), GEODE_SRC(Kick4, kick4, Trigger, 0.5f), GEODE_SRC(Kick8, kick8, Trigger, 0.8f),
    GEODE_SRC(Kick16, kick16, Trigger, 1.2f), GEODE_SRC(Snare2, snare2, Trigger, 0.35f), GEODE_SRC(Snare4, snare4, Trigger, 0.5f),
    GEODE_SRC(HardCut, hardCut, Trigger, 2.5f), GEODE_SRC(Onset, onset, Trigger, 0.22f), GEODE_SRC(Silence, silence, Trigger, 1.5f),
    GEODE_SRC(Returned, returned, Trigger, 2.5f), GEODE_SRC(Surge, surge, Trigger, 2.0f),
    GEODE_SRC(NoteChange, noteChange, Trigger, 0.5f), GEODE_SRC(Legato, legato, Trigger, 0.5f), GEODE_SRC(Reattack, reattack, Trigger, 0.3f),
    GEODE_SRC(NoteRandom, noteRandom, Unit, 0), GEODE_SRC(PitchClass, pitchClass, Unit, 0), GEODE_SRC(PitchHeight, pitchHeight, Unit, 0),
    GEODE_SRC(PitchConfidence, pitchConfidence, Unit, 0), GEODE_SRC(Interval, interval, Signed, 0), GEODE_SRC(Glide, glide, Unit, 0),
    GEODE_SRC(Vibrato, vibrato, Unit, 0), GEODE_SRC(Evenness, evenness, Unit, 0), GEODE_SRC(RunDensity, runDensity, Unit, 0),
    GEODE_EQ(Eq25, 0), GEODE_EQ(Eq40, 1), GEODE_EQ(Eq60, 2), GEODE_EQ(Eq90, 3), GEODE_EQ(Eq150, 4), GEODE_EQ(Eq240, 5),
    GEODE_EQ(Eq370, 6), GEODE_EQ(Eq590, 7), GEODE_EQ(Eq930, 8), GEODE_EQ(Eq1k5, 9), GEODE_EQ(Eq2k3, 10), GEODE_EQ(Eq3k6, 11),
    GEODE_EQ(Eq5k6, 12), GEODE_EQ(Eq8k9, 13), GEODE_EQ(Eq14k, 14), GEODE_EQ(Eq22k, 15),
    GEODE_SRC(BeatPhase, beatPhase, Unit, 0),
    GEODE_SRC(BarPhase, barPhase, Unit, 0),
    GEODE_SRC(PhrasePhase, phrasePhase, Unit, 0),
    GEODE_SRC(BeatConf, beatConf, Unit, 0),
    GEODE_SRC(Bpm, bpm, Scaled, 1.0f / 200.0f),
    GEODE_SRC(Beat, beat, Trigger, 0.25f),
    GEODE_SRC(Downbeat, downbeat, Trigger, 0.5f),
    GEODE_SRC(Phrase, phrase, Trigger, 1.5f),
    GEODE_SRC(FluxLow, fluxLow, Relative, 0),
    GEODE_SRC(FluxMid, fluxMid, Relative, 0),
    GEODE_SRC(FluxHigh, fluxHigh, Relative, 0),
    GEODE_SRC(Hfc, hfc, Relative, 0),
    GEODE_SRC(KeyHue, keyHue, Unit, 0),
    GEODE_SRC(KeyRoot, keyRoot, Unit, 0),
    GEODE_SRC(KeyMajor, keyMajor, Unit, 0),
    GEODE_SRC(KeyClarity, keyClarity, Unit, 0),
    GEODE_SRC(ChordHue, chordHue, Unit, 0),
    GEODE_SRC(Tension, tension, Unit, 0),
    GEODE_SRC(HarmonicChange, harmonicChange, Trigger, 1.0f),
    GEODE_SRC(ScaleRoot, scaleRoot, Unit, 0),
    GEODE_SRC(ScaleBrightness, scaleBrightness, Unit, 0),
    GEODE_SRC(ScaleTension, scaleTension, Unit, 0),
    GEODE_SRC(ScaleFit, scaleFit, Unit, 0),
    GEODE_SRC(Novelty, novelty, Unit, 0),
    GEODE_SRC(SectionChange, sectionChange, Trigger, 2.5f),
    GEODE_SRC(SectionState, sectionState, Scaled, 1.0f / 5.0f),
    GEODE_SRC(SectionId, sectionId, Scaled, 1.0f / 7.0f),
    GEODE_SRC(BuildUp, buildUp, Unit, 0),
    GEODE_SRC(Drop, drop, Trigger, 2.5f),
    GEODE_SRC(FadeGain, fadeGain, Unit, 0),
}};

#undef GEODE_SRC
#undef GEODE_EQ

constexpr bool tableInOrder() {
    for (size_t i = 0; i < kSourceTable.size(); ++i) {
        if (static_cast<size_t>(kSourceTable[i].source) != i) return false;
    }
    return true;
}
static_assert(tableInOrder(), "kSourceTable must list every ReactSource in enum order");

constexpr std::array<ReactSourceInfo, kReactSourceCount> kSourceInfo = {{
    {"BASS", "Bass", "LEVELS", false},
    {"MID", "Mids", "LEVELS", false},
    {"TREBLE", "Treble", "LEVELS", false},
    {"VOL", "Volume", "LEVELS", false},
    {"BASS_ATT", "Bass, fast", "LEVELS", false},
    {"MID_ATT", "Mids, fast", "LEVELS", false},
    {"TREBLE_ATT", "Treble, fast", "LEVELS", false},
    {"VOL_ATT", "Volume, fast", "LEVELS", false},
    {"SUB", "Sub", "LEVELS", false},
    {"SUB_ATT", "Sub, fast", "LEVELS", false},
    {"MID_HIGH", "Presence band", "LEVELS", false},
    {"MID_HIGH_ATT", "Presence band, fast", "LEVELS", false},
    {"LOW", "Low (shaped)", "LEVELS", false},
    {"MIDDLE", "Middle (shaped)", "LEVELS", false},
    {"HIGH", "High (shaped)", "LEVELS", false},
    {"LEVEL", "Level", "LEVELS", false},
    {"BASS_PRESENCE", "Bass activity", "LEVELS", false},
    {"MID_PRESENCE", "Mid activity", "LEVELS", false},
    {"TREBLE_PRESENCE", "Treble activity", "LEVELS", false},
    {"INTENSITY", "Intensity", "LEVELS", false},
    {"PRESENCE", "Music playing", "LEVELS", false},
    {"BASS_TIME", "Bass clock", "CLOCKS", false},
    {"MID_TIME", "Mid clock", "CLOCKS", false},
    {"TREBLE_TIME", "Treble clock", "CLOCKS", false},
    {"CURVED_TIME", "Loudness clock", "CLOCKS", false},
    {"HIT_TIME", "Hit clock", "CLOCKS", false},
    {"NOTE_TIME", "Note clock", "CLOCKS", false},
    {"LOUDNESS", "Loudness", "TEXTURE", false},
    {"CENTROID", "Brightness", "TEXTURE", false},
    {"ROLLOFF", "Roll-off", "TEXTURE", false},
    {"FLATNESS", "Noisiness", "TEXTURE", false},
    {"DENSITY_SLOW", "Density", "TEXTURE", false},
    {"DENSITY_FAST", "Density, fast", "TEXTURE", false},
    {"FLUX", "Spectral flux", "TEXTURE", false},
    {"ENERGY_TREND", "Rise", "TEXTURE", false},
    {"PEAK_FREQ", "Peak pitch", "TEXTURE", false},
    {"PEAK_MAG", "Peak level", "TEXTURE", false},
    {"STEREO_WIDTH", "Stereo width", "TEXTURE", false},
    {"STEREO_PAN", "L/R pan", "TEXTURE", false},
    {"KICK_ENV", "Kick", "DRUMS", false},
    {"SNARE_ENV", "Snare", "DRUMS", false},
    {"HAT_ENV", "Hi-hat", "DRUMS", false},
    {"ONSET_ENV", "Note start", "DRUMS", false},
    {"KICK_TOGGLE", "Kick toggle", "DRUMS", false},
    {"SNARE_TOGGLE", "Snare toggle", "DRUMS", false},
    {"KICK_RANDOM", "Kick random", "DRUMS", false},
    {"KICK", "Kick hit", "DRUMS", true},
    {"SNARE", "Snare hit", "DRUMS", true},
    {"HAT", "Hi-hat hit", "DRUMS", true},
    {"KICK_2", "Every 2nd kick", "DRUMS", true},
    {"KICK_4", "Every 4th kick", "DRUMS", true},
    {"KICK_8", "Every 8th kick", "DRUMS", true},
    {"KICK_16", "Every 16th kick", "DRUMS", true},
    {"SNARE_2", "Every 2nd snare", "DRUMS", true},
    {"SNARE_4", "Every 4th snare", "DRUMS", true},
    {"HARD_CUT", "Big moment", "EVENTS", true},
    {"ONSET", "Onset", "EVENTS", true},
    {"SILENCE", "Silence", "EVENTS", true},
    {"RETURNED", "Sound returns", "EVENTS", true},
    {"SURGE", "Surge", "EVENTS", true},
    {"NOTE_CHANGE", "Note change", "MELODY", true},
    {"LEGATO", "Slide", "MELODY", true},
    {"REATTACK", "Same note again", "MELODY", true},
    {"NOTE_RANDOM", "Note random", "MELODY", false},
    {"PITCH_CLASS", "Pitch class", "MELODY", false, Circle::Hue},
    {"PITCH_HEIGHT", "Pitch height", "MELODY", false},
    {"PITCH_CONFIDENCE", "Pitch clarity", "MELODY", false},
    {"INTERVAL", "Interval", "MELODY", false},
    {"GLIDE", "Glide", "MELODY", false},
    {"VIBRATO", "Vibrato", "MELODY", false},
    {"EVENNESS", "Evenness", "MELODY", false},
    {"RUN_DENSITY", "Busy melody", "MELODY", false},
    {"EQ_25", "25 Hz", "EQ", false},
    {"EQ_40", "40 Hz", "EQ", false},
    {"EQ_60", "60 Hz", "EQ", false},
    {"EQ_90", "90 Hz", "EQ", false},
    {"EQ_150", "150 Hz", "EQ", false},
    {"EQ_240", "240 Hz", "EQ", false},
    {"EQ_370", "370 Hz", "EQ", false},
    {"EQ_590", "590 Hz", "EQ", false},
    {"EQ_930", "930 Hz", "EQ", false},
    {"EQ_1K5", "1.5 kHz", "EQ", false},
    {"EQ_2K3", "2.3 kHz", "EQ", false},
    {"EQ_3K6", "3.6 kHz", "EQ", false},
    {"EQ_5K6", "5.6 kHz", "EQ", false},
    {"EQ_8K9", "8.9 kHz", "EQ", false},
    {"EQ_14K", "14 kHz", "EQ", false},
    {"EQ_22K", "22 kHz", "EQ", false},
    {"BEAT_PHASE", "Beat phase", "RHYTHM", false, Circle::Phase},
    {"BAR_PHASE", "Bar phase", "RHYTHM", false, Circle::Phase},
    {"PHRASE_PHASE", "Phrase phase", "RHYTHM", false, Circle::Phase},
    {"BEAT_CONF", "Beat certainty", "RHYTHM", false},
    {"BPM", "Tempo", "RHYTHM", false},
    {"BEAT", "Beat", "RHYTHM", true},
    {"DOWNBEAT", "Downbeat (bar start)", "RHYTHM", true},
    {"PHRASE", "Phrase start", "RHYTHM", true},
    {"FLUX_LOW", "Low onsets", "RHYTHM", false},
    {"FLUX_MID", "Mid onsets", "RHYTHM", false},
    {"FLUX_HIGH", "High onsets", "RHYTHM", false},
    {"HFC", "Percussive attack", "RHYTHM", false},
    {"KEY_HUE", "Key colour", "HARMONY", false, Circle::Hue},
    {"KEY_ROOT", "Key note", "HARMONY", false, Circle::Hue},
    {"KEY_MAJOR", "Major vs minor", "HARMONY", false},
    {"KEY_CLARITY", "Key clarity", "HARMONY", false},
    {"CHORD_HUE", "Chord colour", "HARMONY", false, Circle::Hue},
    {"TENSION", "Chord tension", "HARMONY", false},
    {"HARMONIC_CHANGE", "Chord change", "HARMONY", true},
    {"SCALE_ROOT", "Scale note", "HARMONY", false, Circle::Hue},
    {"SCALE_BRIGHTNESS", "Scale mood (dark to bright)", "HARMONY", false},
    {"SCALE_TENSION", "Scale tension", "HARMONY", false},
    {"SCALE_FIT", "In a scale", "HARMONY", false},
    {"NOVELTY", "Something new", "FORM", false},
    {"SECTION_CHANGE", "New section", "FORM", true},
    {"SECTION_STATE", "Song part", "FORM", false},
    {"SECTION_ID", "Which section", "FORM", false},
    {"BUILD_UP", "Build-up", "FORM", false},
    {"DROP", "Drop", "FORM", true},
    {"FADE_GAIN", "Player fade", "LEVELS", false},
}};

// A fired trigger always crosses Step/Jump/Toggle's 0.5 edge, however soft the hit; its strength
// shows in how far above 0.5 it lands.
constexpr float kTriggerFloor = 0.5f;

// A NaN or infinite frame time (a clock glitch) is a frame that did not
// happen; anything else is clamped to [0, 0.1] s so one long stall cannot
// fling an envelope or an accumulator.
float sanitizeDt(float dt) { return std::isfinite(dt) ? std::clamp(dt, 0.0f, 0.1f) : 0.0f; }

float onePoleK(float dt, float tau) { return 1.0f - std::exp(-std::max(dt, 0.0f) / std::max(tau, 1e-4f)); }

float field(const GeodeSoundFrame& f, size_t offset) {
    const float v = *reinterpret_cast<const float*>(reinterpret_cast<const unsigned char*>(&f) + offset);
    return std::isfinite(v) ? v : 0.0f;
}

}  // namespace

const ReactSourceInfo& reactSourceInfo(ReactSource s) {
    const int i = std::clamp(static_cast<int>(s), 0, kReactSourceCount - 1);
    return kSourceInfo[static_cast<size_t>(i)];
}

void ReactSignals::reset() {
    values_.fill(0.0f);
    lastClock_.fill(0.0f);
    clocksPrimed_ = false;
    onsetCount_ = 0.0f;
    cutCount_ = 0.0f;
}

void ReactSignals::step(const GeodeSoundFrame& f, float dt) {
    dt = sanitizeDt(dt);
    for (const SourceSpec& spec : kSourceTable) {
        const size_t k = static_cast<size_t>(spec.source);
        const float x = field(f, spec.offset);
        float& v = values_[k];
        switch (spec.kind) {
            case Kind::Relative: v = std::clamp(0.5f * x, 0.0f, 1.0f); break;
            case Kind::Unit: v = std::clamp(x, 0.0f, 1.0f); break;
            case Kind::Signed: v = std::clamp(x, -1.0f, 1.0f); break;
            case Kind::Scaled: v = std::clamp(x * spec.scale, 0.0f, 1.0f); break;
            case Kind::Clock: {
                // A wrap (an hour, or 4096 counts) reads as one still frame, not a backwards leap.
                const float delta = clocksPrimed_ ? x - lastClock_[k] : 0.0f;
                lastClock_[k] = x;
                v = dt > 0.0f && delta > 0.0f ? std::clamp(delta / dt * spec.scale, 0.0f, 1.0f) : (dt > 0.0f ? 0.0f : v);
                break;
            }
            case Kind::Trigger:
                v *= std::exp(-dt / spec.scale);
                if (x > 0.0f) v = std::max(v, kTriggerFloor + (1.0f - kTriggerFloor) * std::clamp(x, 0.0f, 1.0f));
                break;
        }
    }
    clocksPrimed_ = true;
    if (f.onset > 0.0f) onsetCount_ = std::fmod(onsetCount_ + 1.0f, kCountWrap);
    if (f.hardCut > 0.0f) cutCount_ = std::fmod(cutCount_ + 1.0f, kCountWrap);
}

void ReactionEngine::reset() {
    signals_.reset();
    acc_.fill(0.0f);
    out_.fill(0.0f);
    high_.fill(false);
    circ_.fill(0.0f);
    circSource_.fill(0);
    circFrom_.fill(0.0f);
    circBlend_.fill(0.0f);
    for (int i = 0; i < kRoutes; ++i) jumpSeed_[static_cast<size_t>(i)] = 0x9E3779B9u * static_cast<unsigned int>(i + 1);
}

// The next held value for a Jump route, -1..1, seeded per route so a replayed
// feature sequence (an export) draws the same values. Each draw lands at least
// half a unit from the last, so a jump is always visible.
float ReactionEngine::nextJump(int route) {
    unsigned int& x = jumpSeed_[static_cast<size_t>(route)];
    if (x == 0u) x = 0x9E3779B9u * static_cast<unsigned int>(route + 1);
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    const float u = static_cast<float>(x >> 8) / 16777216.0f;  // 0..1
    const float last = acc_[static_cast<size_t>(route)];
    float v = 2.0f * u - 1.0f;
    if (std::fabs(v - last) < 0.5f) v = v > 0.0f ? v - 1.0f : v + 1.0f;
    return std::clamp(v, -1.0f, 1.0f);
}

// Triangle wave of period 4 through -1..1: continuous, bounded, slope 1 at 0.
float ReactionEngine::fold(float accumulator) {
    const float t = std::fmod(accumulator + 1.0f, 4.0f);
    const float u = t < 0.0f ? t + 4.0f : t;
    return u < 2.0f ? u - 1.0f : 3.0f - u;
}

void ReactionEngine::tick(const GeodeSoundFrame& f, float dt) {
    dt = sanitizeDt(dt);
    gain_ = sound::driveGain(f);
    signals_.step(f, dt);
    for (int i = 0; i < kRoutes; ++i) {
        const ReactRoute& r = routes[static_cast<size_t>(i)];
        const size_t k = static_cast<size_t>(i);
        if (!r.enabled || r.target == LfoTarget::None) {
            out_[k] = 0.0f;
            circSource_[k] = 0;
            continue;
        }
        const float src = signals_.value(r.source);
        float target = 0.0f;
        switch (r.mode) {
            case ReactMode::Follow:
                target = shape(src, r.curve) * r.amount;
                break;
            case ReactMode::Step: {
                const bool high = src >= 0.5f;
                if (high && !high_[k]) acc_[k] = std::fmod(acc_[k] + r.amount, 4.0f);
                high_[k] = src >= 0.3f && (high || high_[k]);  // hysteresis: re-arm below 0.3
                target = fold(acc_[k]);
                break;
            }
            case ReactMode::Spin:
                acc_[k] = std::fmod(acc_[k] + shape(src, r.curve) * r.amount * dt, 4.0f);
                target = fold(acc_[k]);
                break;
            case ReactMode::Jump: {
                const bool high = src >= 0.5f;
                if (high && !high_[k]) acc_[k] = nextJump(i);
                high_[k] = src >= 0.3f && (high || high_[k]);
                target = acc_[k] * r.amount;
                break;
            }
            case ReactMode::Toggle: {
                const bool high = src >= 0.5f;
                if (high && !high_[k]) acc_[k] = acc_[k] > 0.5f ? 0.0f : 1.0f;
                high_[k] = src >= 0.3f && (high || high_[k]);
                target = acc_[k] * r.amount;
                break;
            }
        }
        float attack = std::max(r.attackSeconds, 0.0f);
        float release = std::max(r.releaseSeconds, 0.0f);
        if (isLuminanceTarget(r.target)) {
            attack = std::max(attack, kLuminanceMinAttack);
            release = std::max(release, kLuminanceMinRelease);
        }
        const Circle circle = reactSourceInfo(r.source).circle;
        if (r.mode == ReactMode::Follow && circle != Circle::None) {
            // A hue or a phase has no rest to rise from or fall back to, so it glides by the attack
            // alone, on its circle: 0.95 -> 0.05 goes forward through 1.0 = 0.0, never back through 0.5.
            // Smoothing the source before the curve and the amount (not the output after) is what keeps
            // the seam a seam: the output wraps where the source wraps, by `amount`, as the raw source does.
            const float gain = attack <= 1e-4f ? 1.0f : onePoleK(dt, attack);
            const int source = static_cast<int>(r.source) + 1;
            if (circSource_[k] != source) {
                circ_[k] = src;
                circSource_[k] = source;
                circFrom_[k] = out_[k];
                circBlend_[k] = 0.0f;
            } else {
                circ_[k] = circular::follow(circle, circ_[k], src, gain);
            }
            circBlend_[k] += (1.0f - circBlend_[k]) * gain;
            if (circBlend_[k] > 0.999f) circBlend_[k] = 1.0f;
            const float followed = shape(circ_[k], r.curve) * r.amount;
            out_[k] = circFrom_[k] + (followed - circFrom_[k]) * circBlend_[k];
            continue;
        }
        circSource_[k] = 0;
        const float tau = std::fabs(target) > std::fabs(out_[k]) ? attack : release;
        out_[k] += (target - out_[k]) * (tau <= 1e-4f ? 1.0f : onePoleK(dt, tau));
    }
}

SceneParams ReactionEngine::apply(const SceneParams& p) const {
    const float scale = std::clamp(p.reactAmount, 0.0f, 2.0f) * gain_;
    SceneParams o = p;
    if (scale <= 0.0f) return o;
    for (int i = 0; i < kRoutes; ++i) {
        const ReactRoute& r = routes[static_cast<size_t>(i)];
        ModChain chain{};
        if (!r.enabled || r.target == LfoTarget::None || chainOf(r.target, chain)) continue;
        o = applyLfoTarget(o, r.target, out_[static_cast<size_t>(i)] * scale);
    }
    return o;
}

namespace hand {

namespace {

using S = ReactSource;

// Every moment's sources, in Moment order. No source is in two.
struct MomentSources {
    std::array<ReactSource, 8> sources;
    int count;
};
constexpr std::array<MomentSources, kMomentCount> kMomentSources{{
    {{S::KickEnv, S::Kick, S::Kick2, S::Kick4, S::Kick8, S::Kick16, S::KickToggle, S::KickRandom}, 8},
    {{S::SnareEnv, S::Snare, S::Snare2, S::Snare4, S::SnareToggle}, 5},
    {{S::HatEnv, S::Hat}, 2},
    {{S::Bass, S::BassAtt, S::Sub, S::SubAtt, S::Low, S::BassPresence, S::BassTime}, 7},
    {{S::Drop, S::Surge, S::Returned}, 3},
    {{S::BuildUp, S::EnergyTrend}, 2},
    {{S::HarmonicChange, S::ChordHue, S::Tension}, 3},
    {{S::SectionChange, S::HardCut, S::Phrase, S::SectionState, S::SectionId}, 5},
    {{S::NoteChange, S::Reattack, S::Legato, S::NoteRandom, S::PitchClass, S::PitchHeight, S::Glide, S::Vibrato}, 8},
    {{S::Loudness, S::Vol, S::VolAtt, S::Level, S::Intensity, S::CurvedTime}, 6},
}};

// Each source's moment, or -1: looked up per route per frame by the director, so a table, not a search.
constexpr std::array<int, kReactSourceCount> kOwners = [] {
    std::array<int, kReactSourceCount> owners{};
    for (int& o : owners) o = -1;
    for (int m = 0; m < kMomentCount; ++m) {
        const MomentSources& ms = kMomentSources[static_cast<size_t>(m)];
        for (int i = 0; i < ms.count; ++i) owners[static_cast<size_t>(ms.sources[static_cast<size_t>(i)])] = m;
    }
    return owners;
}();

std::atomic<std::uint32_t> gMask{0};

}  // namespace

int momentOf(ReactSource s) {
    const int i = static_cast<int>(s);
    return i >= 0 && i < kReactSourceCount ? kOwners[static_cast<size_t>(i)] : -1;
}

ReactSourceMask sources(std::uint32_t mask) {
    ReactSourceMask out;
    for (int i = 0; i < kReactSourceCount; ++i) {
        const int m = kOwners[static_cast<size_t>(i)];
        const std::uint32_t owner = m < 0 ? kUnclaimed : (1u << m);
        out[static_cast<size_t>(i)] = (mask & owner) != 0u;
    }
    return out;
}

// One store of the effective mask, so a reader never sees a mask from one push and has-user from another.
void setGlobal(std::uint32_t mask, bool hasUser) { gMask.store(hasUser ? mask & kAll : 0u, std::memory_order_relaxed); }
std::uint32_t global() { return gMask.load(std::memory_order_relaxed); }

}  // namespace hand

}  // namespace geode::viz
