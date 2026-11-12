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
    Derived,   // no field of its own: ReactSignals::step works it out from others (a pulse decays over `scale` seconds)
};

struct SourceSpec {
    ReactSource source;
    size_t offset;
    Kind kind;
    float scale;
};

#define GEODE_SRC(src, field, kind, scale) SourceSpec{ReactSource::src, offsetof(GeodeSoundFrame, field), Kind::kind, scale}
#define GEODE_DER(src, scale) SourceSpec{ReactSource::src, 0, Kind::Derived, scale}
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
    GEODE_SRC(Bpm, bpm, Scaled, 1.0f / GEODE_SOUND_BPM_FASTEST),
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
    GEODE_SRC(Transient, transient, Trigger, 0.22f),
    GEODE_SRC(TransientEnv, transientEnv, Unit, 0),
    GEODE_SRC(TransientSharpness, transientSharpness, Unit, 0),
    GEODE_DER(BarHold, 0), GEODE_DER(BarFlip, 0),
    GEODE_DER(Bar2Hold, 0), GEODE_DER(Bar2Pulse, 0.8f), GEODE_DER(Bar2Flip, 0),
    GEODE_DER(Bar4Hold, 0), GEODE_DER(Bar4Pulse, 1.2f), GEODE_DER(Bar4Flip, 0),
    GEODE_DER(PhraseHold, 0), GEODE_DER(PhraseFlip, 0),
    GEODE_DER(MelodyContour, 0), GEODE_DER(MelodyNote, 0.4f), GEODE_DER(MelodyPresence, 0),
    GEODE_DER(DegreeAccent, 0.6f), GEODE_DER(Valence, 0), GEODE_DER(FeltTension, 0),
    GEODE_DER(TensionRise, 0),
    GEODE_DER(FeltEnergy, 0), GEODE_DER(KeySharpness, 0), GEODE_DER(FeelingKind, 0),
    GEODE_DER(FeelWonder, 0), GEODE_DER(FeelPeace, 0), GEODE_DER(FeelPower, 0), GEODE_DER(FeelJoy, 0), GEODE_DER(FeelSadness, 0),
    GEODE_DER(InIntro, 0), GEODE_DER(InVerse, 0), GEODE_DER(InBuild, 0), GEODE_DER(InDrop, 0), GEODE_DER(InBreakdown, 0),
    GEODE_DER(InOutro, 0),
    GEODE_DER(SectionReturn, 2.5f), GEODE_DER(SectionProgress, 0),
    GEODE_DER(ChordMotion, 0),
}};

#undef GEODE_SRC
#undef GEODE_DER
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
    {"TRANSIENT", "Transient hit", "EVENTS", true},
    {"TRANSIENT_ENV", "Transient", "DRUMS", false},
    {"TRANSIENT_SHARPNESS", "Attack sharpness", "TEXTURE", false},
    {"BAR_HOLD", "Every bar: new value", "RHYTHM", false},
    {"BAR_FLIP", "Every bar: flip", "RHYTHM", false},
    {"BAR2_HOLD", "Every 2 bars: new value", "RHYTHM", false},
    {"BAR2_PULSE", "Every 2 bars", "RHYTHM", true},
    {"BAR2_FLIP", "Every 2 bars: flip", "RHYTHM", false},
    {"BAR4_HOLD", "Every 4 bars: new value", "RHYTHM", false},
    {"BAR4_PULSE", "Every 4 bars", "RHYTHM", true},
    {"BAR4_FLIP", "Every 4 bars: flip", "RHYTHM", false},
    {"PHRASE_HOLD", "Every phrase: new value", "RHYTHM", false},
    {"PHRASE_FLIP", "Every phrase: flip", "RHYTHM", false},
    {"MELODY_CONTOUR", "Melody rising or falling", "MELODY", false},
    {"MELODY_NOTE", "Melody note", "MELODY", true},
    {"MELODY_PRESENCE", "Melody playing", "MELODY", false},
    {"DEGREE_ACCENT", "Scale step change", "MELODY", true},
    {"VALENCE", "Mood (sad to happy)", "HARMONY", false},
    {"FELT_TENSION", "Felt tension", "HARMONY", false},
    {"TENSION_RISE", "Tension rising or easing", "HARMONY", false},
    {"FELT_ENERGY", "Felt energy", "HARMONY", false},
    {"KEY_SHARPNESS", "Key character (mellow to brilliant)", "HARMONY", false},
    {"FEELING_KIND", "Kind of feeling", "HARMONY", false},
    {"FEEL_WONDER", "Feeling: wonder", "HARMONY", false},
    {"FEEL_PEACE", "Feeling: peace", "HARMONY", false},
    {"FEEL_POWER", "Feeling: power", "HARMONY", false},
    {"FEEL_JOY", "Feeling: joy", "HARMONY", false},
    {"FEEL_SADNESS", "Feeling: sadness", "HARMONY", false},
    {"IN_INTRO", "In the intro", "FORM", false},
    {"IN_VERSE", "In a verse", "FORM", false},
    {"IN_BUILD", "In a build", "FORM", false},
    {"IN_DROP", "In a drop", "FORM", false},
    {"IN_BREAKDOWN", "In a breakdown", "FORM", false},
    {"IN_OUTRO", "In the outro", "FORM", false},
    {"SECTION_RETURN", "A section comes back", "FORM", true},
    {"SECTION_PROGRESS", "Through the section", "FORM", false},
    {"CHORD_MOTION", "Chords moving", "HARMONY", false},
}};

// A fired trigger always crosses Step/Jump/Toggle's 0.5 edge, however soft the hit; its strength
// shows in how far above 0.5 it lands.
constexpr float kTriggerFloor = 0.5f;

// ── The derived sources ─────────────────────────────────────────────────────────────────────────────
//
// Bar counts. The engine fires `downbeat` and `phrase` only while Rhythm is locked on a beat it heard, so a
// count comes only from the music's own bars: no beat heard, no count, and the holds and flips stay where
// they are. Below Rhythm's unlock line (its kLockOff) a downbeat is not trusted either. The bar in the phrase
// is the phrase phase in eighths at the downbeat (Rhythm builds that phase from it), so "every 4 bars" lands on
// bars 1 and 5 of the phrase, and a new section, which restarts the phrase, restarts the counts with it. A
// bar is Rhythm's bar, whatever metre it reads (four beats or three).
constexpr float kTrustedBeat = 0.2f;
constexpr int kBarsInPhrase = 8;
// Each count: how many bars it spans (the phrase's is counted on the phrase trigger), its hold, its pulse (none
// for every bar and every phrase: those pulses are Downbeat and Phrase) and its flip.
struct BarCount {
    int bars;
    ReactSource hold;
    bool hasPulse;
    ReactSource pulse;
    ReactSource flip;
};
constexpr std::array<BarCount, 4> kBarCounts{{
    {1, ReactSource::BarHold, false, ReactSource::BarHold, ReactSource::BarFlip},
    {2, ReactSource::Bar2Hold, true, ReactSource::Bar2Pulse, ReactSource::Bar2Flip},
    {4, ReactSource::Bar4Hold, true, ReactSource::Bar4Pulse, ReactSource::Bar4Flip},
    {kBarsInPhrase, ReactSource::PhraseHold, false, ReactSource::PhraseHold, ReactSource::PhraseFlip},
}};
// A hold's value on a count: the golden-ratio sequence on the frame's bar (or phrase) number, offset per count,
// so each value lands far from the last and two counts on the same bar do not share one. Read off the frame's
// own count, so the same song gives the same values on the screen and in an export.
constexpr float kGoldenStep = 0.618034f;

// The melody line. A note sounds while the frame has a pitch (`pitch` > 0: past the engine's pitch threshold).
// The contour is a fast follower of that pitch over a slow one, in semitones over kContourSemitones: a smoothed
// slope over the last second or two that reads +1 once the tune has climbed a fourth above where it was. While
// no note sounds it eases back to 0, and once the tune has been gone a while the next note starts a new line
// rather than reading the leap from the last one as a rise.
constexpr float kContourFastSeconds = 0.15f;
constexpr float kContourSlowSeconds = 1.5f;
constexpr float kContourSemitones = 5.0f;
constexpr float kContourRestSeconds = 0.6f;
constexpr float kPresenceRiseSeconds = 0.15f;
constexpr float kPresenceFallSeconds = 0.8f;
constexpr float kLineEnds = 0.05f;
// The tension's rise: a fast follower of the chord tension over a slow one, so a passage tightening over a few
// seconds reads up to +1 and one resolving down to -1 (kTensionRiseSpan of tension between the two reads 1).
constexpr float kTensionFastSeconds = 0.4f;
constexpr float kTensionSlowSeconds = 4.0f;
constexpr float kTensionRiseSpan = 0.25f;

// The kind of feeling: a new kind leads once its share is this far above the one leading now, so two kinds
// level with each other do not flicker the source. A kind's own share reads 1 at kShareFull times its even
// share (1/9).
constexpr float kKindMargin = 0.02f;
constexpr float kShareFull = 3.0f;
constexpr int kKindCount = sound::Emotion::kGems;

// The song part's states, in GEODE_SECTION_* order.
constexpr std::array<ReactSource, 6> kSectionStates{{ReactSource::InIntro, ReactSource::InVerse, ReactSource::InBuild,
                                                     ReactSource::InDrop, ReactSource::InBreakdown, ReactSource::InOutro}};
static_assert(GEODE_SECTION_OUTRO == 5, "a state source for each section state");
// Form names at most this many sections at once (its kSections); a sectionId is one of them.
constexpr int kSectionIds = 8;

// Section progress. A section begins where Form's sectionBars starts again (a new state) or where it says a new
// section began (sectionChange, which fires a bar after the boundary, so that section has a bar behind it). Its
// length is not known until it ends, so the progress is measured against how long this song's sections have run:
// each one that ends moves that halfway to its own length. A section shorter than kShortestSection bars (a
// two-bar build that tips straight into its drop) teaches nothing. Past the expected length the progress holds
// at 1 until the next section. The bar's own phase adds the part of a bar while the beat is trusted.
constexpr float kShortestSection = 4.0f;
constexpr float kLongestSection = 64.0f;
constexpr float kSectionLearn = 0.5f;

// Chord motion: the chord's speed round the circle of fifths (chordHue, the shorter way across the seam), eased
// over about a second and a half, so one change reads as a swell and a run of changes as a steady motion. A
// quarter of the circle a second (three fifths) reads 1.
constexpr float kChordMotionSeconds = 1.5f;
constexpr float kChordMotionFull = 0.25f;

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
    melodyFast_ = 0.0f;
    melodySlow_ = 0.0f;
    melodyPrimed_ = false;
    tensionFast_ = 0.0f;
    tensionSlow_ = 0.0f;
    feelingKind_ = -1;
    seenSections_ = 0;
    lastSection_ = -1;
    lastSectionBars_ = 0.0f;
    barsInSection_ = 0.0f;
    sectionLength_ = kFirstSectionBars;
    sectionPrimed_ = false;
    lastChordHue_ = 0.0f;
    chordSpeed_ = 0.0f;
    chordPrimed_ = false;
    onsetCount_ = 0.0f;
    cutCount_ = 0.0f;
}

void ReactSignals::step(const GeodeSoundFrame& f, float dt, const sound::Emotion::State* feeling) {
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
            case Kind::Derived:
                // A derived trigger decays here and is raised below; a held value keeps what it had.
                if (spec.scale > 0.0f) v *= std::exp(-dt / spec.scale);
                break;
        }
    }
    clocksPrimed_ = true;
    stepDerived(f, dt, feeling);
    if (f.onset > 0.0f) onsetCount_ = std::fmod(onsetCount_ + 1.0f, kCountWrap);
    if (f.hardCut > 0.0f) cutCount_ = std::fmod(cutCount_ + 1.0f, kCountWrap);
}

void ReactSignals::stepDerived(const GeodeSoundFrame& f, float dt, const sound::Emotion::State* feeling) {
    const auto clean = [](float x) { return std::isfinite(x) ? x : 0.0f; };
    const auto fire = [this](ReactSource s, float strength) {
        float& v = values_[static_cast<size_t>(s)];
        v = std::max(v, kTriggerFloor + (1.0f - kTriggerFloor) * std::clamp(strength, 0.0f, 1.0f));
    };

    // Bar counts, on the music's own downbeats only.
    const float downbeat = std::max(clean(f.downbeat), clean(f.phrase));
    if (downbeat > 0.0f && clean(f.beatConf) >= kTrustedBeat) {
        const bool phraseStart = clean(f.phrase) > 0.0f;
        const float eighths = std::clamp(clean(f.phrasePhase), 0.0f, 1.0f) * static_cast<float>(kBarsInPhrase);
        const int inPhrase = phraseStart ? 0 : static_cast<int>(std::lround(eighths)) % kBarsInPhrase;
        for (size_t c = 0; c < kBarCounts.size(); ++c) {
            const BarCount& count = kBarCounts[c];
            if (inPhrase % count.bars != 0) continue;
            const float number = count.bars == kBarsInPhrase ? clean(f.phraseIndex) : clean(f.barCount);
            const float hold = 0.5f + 0.25f * static_cast<float>(c) + number * kGoldenStep;
            values_[static_cast<size_t>(count.hold)] = hold - std::floor(hold);
            float& flip = values_[static_cast<size_t>(count.flip)];
            flip = flip > 0.5f ? 0.0f : 1.0f;
            if (count.hasPulse) fire(count.pulse, downbeat);
        }
    }

    // The melody line.
    const float pitch = clean(f.pitch);
    const bool voiced = pitch > 0.0f;
    float& presence = values_[static_cast<size_t>(ReactSource::MelodyPresence)];
    presence += ((voiced ? 1.0f : 0.0f) - presence) * onePoleK(dt, voiced ? kPresenceRiseSeconds : kPresenceFallSeconds);
    float& contour = values_[static_cast<size_t>(ReactSource::MelodyContour)];
    if (voiced) {
        if (!melodyPrimed_) {
            melodyFast_ = pitch;
            melodySlow_ = pitch;
            melodyPrimed_ = true;
        }
        melodyFast_ += (pitch - melodyFast_) * onePoleK(dt, kContourFastSeconds);
        melodySlow_ += (pitch - melodySlow_) * onePoleK(dt, kContourSlowSeconds);
        contour = std::clamp((melodyFast_ - melodySlow_) / kContourSemitones, -1.0f, 1.0f);
    } else {
        contour -= contour * onePoleK(dt, kContourRestSeconds);
        if (presence < kLineEnds) melodyPrimed_ = false;
    }
    const float note = std::max(clean(f.noteChange), clean(f.reattack));
    if (note > 0.0f) fire(ReactSource::MelodyNote, note);

    // The tension's rise.
    const float tension = std::clamp(clean(f.tension), 0.0f, 1.0f);
    tensionFast_ += (tension - tensionFast_) * onePoleK(dt, kTensionFastSeconds);
    tensionSlow_ += (tension - tensionSlow_) * onePoleK(dt, kTensionSlowSeconds);
    values_[static_cast<size_t>(ReactSource::TensionRise)] = std::clamp((tensionFast_ - tensionSlow_) / kTensionRiseSpan, -1.0f, 1.0f);

    // The feeling, as the director heard it; with none heard its levels rest and its kind holds.
    float valence = 0.0f;
    float felt = 0.0f;
    float energy = 0.0f;
    float sharpness = 0.0f;
    sound::Emotion::GemsVec gems{};
    if (feeling != nullptr) {
        if (clean(feeling->degreeAccent) > 0.0f) fire(ReactSource::DegreeAccent, feeling->degreeAccent);
        valence = std::clamp(clean(feeling->valence), -1.0f, 1.0f);
        felt = std::clamp(clean(feeling->tension), 0.0f, 1.0f);
        energy = std::clamp(clean(feeling->energy), 0.0f, 1.0f);
        sharpness = std::clamp(clean(feeling->sharpness), -1.0f, 1.0f);
        int lead = 0;
        for (int k = 0; k < kKindCount; ++k) {
            gems[static_cast<size_t>(k)] = std::clamp(clean(feeling->gems[static_cast<size_t>(k)]), 0.0f, 1.0f);
            if (gems[static_cast<size_t>(k)] > gems[static_cast<size_t>(lead)]) lead = k;
        }
        if (feelingKind_ < 0 || gems[static_cast<size_t>(lead)] > gems[static_cast<size_t>(feelingKind_)] + kKindMargin) feelingKind_ = lead;
    }
    values_[static_cast<size_t>(ReactSource::Valence)] = valence;
    values_[static_cast<size_t>(ReactSource::FeltTension)] = felt;
    values_[static_cast<size_t>(ReactSource::FeltEnergy)] = energy;
    values_[static_cast<size_t>(ReactSource::KeySharpness)] = sharpness;
    values_[static_cast<size_t>(ReactSource::FeelingKind)] =
        feelingKind_ < 0 ? 0.0f : static_cast<float>(feelingKind_) / static_cast<float>(kKindCount - 1);
    const auto share = [&gems](sound::Emotion::Gems k) {
        return std::clamp(gems[static_cast<size_t>(k)] * static_cast<float>(kKindCount) / kShareFull, 0.0f, 1.0f);
    };
    values_[static_cast<size_t>(ReactSource::FeelWonder)] = share(sound::Emotion::kWonder);
    values_[static_cast<size_t>(ReactSource::FeelPeace)] = share(sound::Emotion::kPeace);
    values_[static_cast<size_t>(ReactSource::FeelPower)] = share(sound::Emotion::kPower);
    values_[static_cast<size_t>(ReactSource::FeelJoy)] = share(sound::Emotion::kJoy);
    values_[static_cast<size_t>(ReactSource::FeelSadness)] = share(sound::Emotion::kSadness);

    // The song part, as Form names it.
    const int state = std::clamp(static_cast<int>(std::lround(clean(f.sectionState))), 0, static_cast<int>(kSectionStates.size()) - 1);
    for (size_t k = 0; k < kSectionStates.size(); ++k) values_[static_cast<size_t>(kSectionStates[k])] = static_cast<int>(k) == state ? 1.0f : 0.0f;

    // A section comes back: Form gives a new section the number of one it heard before (it never names the one
    // just left again, so a changed number is always a new section, an old number a return).
    const int section = std::clamp(static_cast<int>(std::lround(clean(f.sectionId))), 0, kSectionIds - 1);
    if (lastSection_ >= 0 && section != lastSection_ && (seenSections_ & (1u << section)) != 0u) fire(ReactSource::SectionReturn, 1.0f);
    seenSections_ |= 1u << section;
    lastSection_ = section;

    // How far through the section.
    const float bars = std::clamp(clean(f.sectionBars), 0.0f, ReactSignals::kCountWrap);
    const bool changed = clean(f.sectionChange) > 0.0f;
    const bool turned = sectionPrimed_ && bars < lastSectionBars_;
    if (sectionPrimed_ && (changed || turned)) {
        if (barsInSection_ >= kShortestSection) {
            sectionLength_ += (std::min(barsInSection_, kLongestSection) - sectionLength_) * kSectionLearn;
        }
        barsInSection_ = changed ? 1.0f : bars;
    } else {
        barsInSection_ += sectionPrimed_ ? bars - lastSectionBars_ : bars;
    }
    lastSectionBars_ = bars;
    sectionPrimed_ = true;
    const float inBar = clean(f.beatConf) >= kTrustedBeat ? std::clamp(clean(f.barPhase), 0.0f, 1.0f) : 0.0f;
    values_[static_cast<size_t>(ReactSource::SectionProgress)] = std::clamp((barsInSection_ + inBar) / sectionLength_, 0.0f, 1.0f);

    // The chords' motion round the circle of fifths.
    const float hue = clean(f.chordHue);
    if (dt > 0.0f) {
        float step = chordPrimed_ ? hue - lastChordHue_ : 0.0f;
        step -= std::round(step);
        chordSpeed_ += (std::fabs(step) / dt - chordSpeed_) * onePoleK(dt, kChordMotionSeconds);
        lastChordHue_ = hue;
        chordPrimed_ = true;
    }
    values_[static_cast<size_t>(ReactSource::ChordMotion)] = std::clamp(chordSpeed_ / kChordMotionFull, 0.0f, 1.0f);
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
    settle_.fill(0.0f);
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

void ReactionEngine::tick(const GeodeSoundFrame& f, float dt, const sound::Emotion::State* feeling) {
    dt = sanitizeDt(dt);
    gain_ = sound::driveGain(f);
    signals_.step(f, dt, feeling);
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
            case ReactMode::JumpSettled: {
                // As Jump, but an edge counts only once the last value has had its glide; one that comes
                // sooner is passed over (it re-arms like any edge), never saved for later.
                settle_[k] = std::max(0.0f, settle_[k] - dt);
                const bool high = src >= 0.5f;
                if (high && !high_[k] && settle_[k] <= 0.0f) {
                    acc_[k] = nextJump(i);
                    settle_[k] = std::max(r.attackSeconds, 0.0f) + std::max(r.releaseSeconds, 0.0f);
                }
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
    std::array<ReactSource, 12> sources;
    int count;
};
constexpr std::array<MomentSources, kMomentCount> kMomentSources{{
    {{S::KickEnv, S::Kick, S::Kick2, S::Kick4, S::Kick8, S::Kick16, S::KickToggle, S::KickRandom}, 8},
    {{S::SnareEnv, S::Snare, S::Snare2, S::Snare4, S::SnareToggle}, 5},
    {{S::HatEnv, S::Hat}, 2},
    {{S::Bass, S::BassAtt, S::Sub, S::SubAtt, S::Low, S::BassPresence, S::BassTime}, 7},
    {{S::Drop, S::Surge, S::Returned, S::InDrop}, 4},
    {{S::BuildUp, S::EnergyTrend, S::InBuild}, 3},
    {{S::HarmonicChange, S::ChordHue, S::Tension, S::ChordMotion}, 4},
    {{S::SectionChange, S::HardCut, S::Phrase, S::SectionState, S::SectionId, S::SectionReturn, S::SectionProgress, S::InIntro,
      S::InVerse, S::InBreakdown, S::InOutro},
     11},
    {{S::NoteChange, S::Reattack, S::Legato, S::NoteRandom, S::PitchClass, S::PitchHeight, S::Glide, S::Vibrato,
      S::MelodyContour, S::MelodyNote, S::MelodyPresence, S::DegreeAccent},
     12},
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

ReactSource trigger(Moment m) {
    const int i = static_cast<int>(m);
    if (i < 0 || i >= kMomentCount) return ReactSource::Bass;
    const MomentSources& ms = kMomentSources[static_cast<size_t>(i)];
    for (int k = 0; k < ms.count; ++k) {
        if (reactSourceInfo(ms.sources[static_cast<size_t>(k)]).event) return ms.sources[static_cast<size_t>(k)];
    }
    return ms.sources[0];
}

}  // namespace hand

std::uint32_t MomentWatch::step(const std::array<float, kMomentCount>& triggers) {
    std::uint32_t came = 0;
    for (int m = 0; m < kMomentCount; ++m) {
        const size_t k = static_cast<size_t>(m);
        const float v = std::isfinite(triggers[k]) ? triggers[k] : 0.0f;
        const bool high = v >= kRise;
        if (high && !high_[k]) came |= 1u << m;
        high_[k] = v >= kRearm && (high || high_[k]);
    }
    return came;
}

std::uint32_t MomentWatch::step(const ReactSignals& signals) {
    std::array<float, kMomentCount> triggers{};
    for (int m = 0; m < kMomentCount; ++m) triggers[static_cast<size_t>(m)] = signals.value(hand::trigger(static_cast<Moment>(m)));
    return step(triggers);
}

}  // namespace geode::viz
