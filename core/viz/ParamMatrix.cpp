#include "viz/ParamMatrix.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace geode::viz::matrix {

namespace {

using K = SourceKind;

#define GEODE_SRC(src, name, label, group, field, kind, scale) \
    SourceRow{ReactSource::src, name, label, group, offsetof(GeodeSoundFrame, field), K::kind, scale}
#define GEODE_EQ(src, name, label, band) \
    SourceRow{ReactSource::src, name, label, "EQ", offsetof(GeodeSoundFrame, eq) + (band) * sizeof(float), K::Relative, 0}

// The rows: one per GeodeSoundFrame value, in ReactSource order. Clock scales: bandTime runs at its
// band's att (~1/s typical), curvedTime at vol_att^2.5, hitTime at hits per second, noteTime at notes
// per second; each scaled so a busy passage reads near 1. Trigger scales are the held envelope's fall:
// short for the fast drums, seconds for the structural events.
constexpr std::array<SourceRow, kReactSourceCount> kSources = {{
    GEODE_SRC(Bass, "BASS", "Bass", "LEVELS", bass, Relative, 0),
    GEODE_SRC(Mid, "MID", "Mids", "LEVELS", mid, Relative, 0),
    GEODE_SRC(Treble, "TREBLE", "Treble", "LEVELS", treble, Relative, 0),
    GEODE_SRC(Vol, "VOL", "Volume", "LEVELS", vol, Relative, 0),
    GEODE_SRC(BassAtt, "BASS_ATT", "Bass, fast", "LEVELS", bassAtt, Relative, 0),
    GEODE_SRC(MidAtt, "MID_ATT", "Mids, fast", "LEVELS", midAtt, Relative, 0),
    GEODE_SRC(TrebleAtt, "TREBLE_ATT", "Treble, fast", "LEVELS", trebleAtt, Relative, 0),
    GEODE_SRC(VolAtt, "VOL_ATT", "Volume, fast", "LEVELS", volAtt, Relative, 0),
    GEODE_SRC(Sub, "SUB", "Sub", "LEVELS", sub, Relative, 0),
    GEODE_SRC(SubAtt, "SUB_ATT", "Sub, fast", "LEVELS", subAtt, Relative, 0),
    GEODE_SRC(MidHigh, "MID_HIGH", "Presence band", "LEVELS", midHigh, Relative, 0),
    GEODE_SRC(MidHighAtt, "MID_HIGH_ATT", "Presence band, fast", "LEVELS", midHighAtt, Relative, 0),
    GEODE_SRC(Low, "LOW", "Low (shaped)", "LEVELS", low, Unit, 0),
    GEODE_SRC(Middle, "MIDDLE", "Middle (shaped)", "LEVELS", middle, Unit, 0),
    GEODE_SRC(High, "HIGH", "High (shaped)", "LEVELS", high, Unit, 0),
    GEODE_SRC(Level, "LEVEL", "Level", "LEVELS", level, Unit, 0),
    GEODE_SRC(BassPresence, "BASS_PRESENCE", "Bass activity", "LEVELS", bassPresence, Unit, 0),
    GEODE_SRC(MidPresence, "MID_PRESENCE", "Mid activity", "LEVELS", midPresence, Unit, 0),
    GEODE_SRC(TreblePresence, "TREBLE_PRESENCE", "Treble activity", "LEVELS", treblePresence, Unit, 0),
    GEODE_SRC(Intensity, "INTENSITY", "Intensity", "LEVELS", intensity, Unit, 0),
    GEODE_SRC(Presence, "PRESENCE", "Music playing", "LEVELS", presence, Unit, 0),
    GEODE_SRC(BassTime, "BASS_TIME", "Bass clock", "CLOCKS", bassTime, Clock, 0.5f),
    GEODE_SRC(MidTime, "MID_TIME", "Mid clock", "CLOCKS", midTime, Clock, 0.5f),
    GEODE_SRC(TrebleTime, "TREBLE_TIME", "Treble clock", "CLOCKS", trebleTime, Clock, 0.5f),
    GEODE_SRC(CurvedTime, "CURVED_TIME", "Loudness clock", "CLOCKS", curvedTime, Clock, 0.5f),
    GEODE_SRC(HitTime, "HIT_TIME", "Hit clock", "CLOCKS", hitTime, Clock, 0.25f),
    GEODE_SRC(NoteTime, "NOTE_TIME", "Note clock", "CLOCKS", noteTime, Clock, 0.125f),
    GEODE_SRC(Loudness, "LOUDNESS", "Loudness", "TEXTURE", loudness, Unit, 0),
    GEODE_SRC(Centroid, "CENTROID", "Brightness", "TEXTURE", centroid, Unit, 0),
    GEODE_SRC(Rolloff, "ROLLOFF", "Roll-off", "TEXTURE", rolloff, Unit, 0),
    GEODE_SRC(Flatness, "FLATNESS", "Noisiness", "TEXTURE", flatness, Unit, 0),
    GEODE_SRC(DensitySlow, "DENSITY_SLOW", "Density", "TEXTURE", densitySlow, Unit, 0),
    GEODE_SRC(DensityFast, "DENSITY_FAST", "Density, fast", "TEXTURE", densityFast, Unit, 0),
    GEODE_SRC(Flux, "FLUX", "Spectral flux", "TEXTURE", flux, Relative, 0),
    GEODE_SRC(EnergyTrend, "ENERGY_TREND", "Rise", "TEXTURE", energyTrend, Unit, 0),
    GEODE_SRC(PeakFreq, "PEAK_FREQ", "Peak pitch", "TEXTURE", peakFreq, Unit, 0),
    GEODE_SRC(PeakMag, "PEAK_MAG", "Peak level", "TEXTURE", peakMag, Unit, 0),
    GEODE_SRC(StereoWidth, "STEREO_WIDTH", "Stereo width", "TEXTURE", stereoWidth, Unit, 0),
    GEODE_SRC(StereoPan, "STEREO_PAN", "L/R pan", "TEXTURE", stereoPan, Signed, 0),
    GEODE_SRC(KickEnv, "KICK_ENV", "Kick", "DRUMS", kickEnv, Unit, 0),
    GEODE_SRC(SnareEnv, "SNARE_ENV", "Snare", "DRUMS", snareEnv, Unit, 0),
    GEODE_SRC(HatEnv, "HAT_ENV", "Hi-hat", "DRUMS", hatEnv, Unit, 0),
    GEODE_SRC(OnsetEnv, "ONSET_ENV", "Note start", "DRUMS", onsetEnv, Unit, 0),
    GEODE_SRC(KickToggle, "KICK_TOGGLE", "Kick toggle", "DRUMS", kickToggle, Unit, 0),
    GEODE_SRC(SnareToggle, "SNARE_TOGGLE", "Snare toggle", "DRUMS", snareToggle, Unit, 0),
    GEODE_SRC(KickRandom, "KICK_RANDOM", "Kick random", "DRUMS", kickRandom, Unit, 0),
    GEODE_SRC(Kick, "KICK", "Kick hit", "DRUMS", kick, Trigger, 0.25f),
    GEODE_SRC(Snare, "SNARE", "Snare hit", "DRUMS", snare, Trigger, 0.25f),
    GEODE_SRC(Hat, "HAT", "Hi-hat hit", "DRUMS", hat, Trigger, 0.15f),
    GEODE_SRC(Kick2, "KICK_2", "Every 2nd kick", "DRUMS", kick2, Trigger, 0.35f),
    GEODE_SRC(Kick4, "KICK_4", "Every 4th kick", "DRUMS", kick4, Trigger, 0.5f),
    GEODE_SRC(Kick8, "KICK_8", "Every 8th kick", "DRUMS", kick8, Trigger, 0.8f),
    GEODE_SRC(Kick16, "KICK_16", "Every 16th kick", "DRUMS", kick16, Trigger, 1.2f),
    GEODE_SRC(Snare2, "SNARE_2", "Every 2nd snare", "DRUMS", snare2, Trigger, 0.35f),
    GEODE_SRC(Snare4, "SNARE_4", "Every 4th snare", "DRUMS", snare4, Trigger, 0.5f),
    GEODE_SRC(HardCut, "HARD_CUT", "Big moment", "EVENTS", hardCut, Trigger, 2.5f),
    GEODE_SRC(Onset, "ONSET", "Onset", "EVENTS", onset, Trigger, 0.22f),
    GEODE_SRC(Silence, "SILENCE", "Silence", "EVENTS", silence, Trigger, 1.5f),
    GEODE_SRC(Returned, "RETURNED", "Sound returns", "EVENTS", returned, Trigger, 2.5f),
    GEODE_SRC(Surge, "SURGE", "Surge", "EVENTS", surge, Trigger, 2.0f),
    GEODE_SRC(NoteChange, "NOTE_CHANGE", "Note change", "MELODY", noteChange, Trigger, 0.5f),
    GEODE_SRC(Legato, "LEGATO", "Slide", "MELODY", legato, Trigger, 0.5f),
    GEODE_SRC(Reattack, "REATTACK", "Same note again", "MELODY", reattack, Trigger, 0.3f),
    GEODE_SRC(NoteRandom, "NOTE_RANDOM", "Note random", "MELODY", noteRandom, Unit, 0),
    GEODE_SRC(PitchClass, "PITCH_CLASS", "Pitch class", "MELODY", pitchClass, Unit, 0),
    GEODE_SRC(PitchHeight, "PITCH_HEIGHT", "Pitch height", "MELODY", pitchHeight, Unit, 0),
    GEODE_SRC(PitchConfidence, "PITCH_CONFIDENCE", "Pitch clarity", "MELODY", pitchConfidence, Unit, 0),
    GEODE_SRC(Interval, "INTERVAL", "Interval", "MELODY", interval, Signed, 0),
    GEODE_SRC(Glide, "GLIDE", "Glide", "MELODY", glide, Unit, 0),
    GEODE_SRC(Vibrato, "VIBRATO", "Vibrato", "MELODY", vibrato, Unit, 0),
    GEODE_SRC(Evenness, "EVENNESS", "Evenness", "MELODY", evenness, Unit, 0),
    GEODE_SRC(RunDensity, "RUN_DENSITY", "Busy melody", "MELODY", runDensity, Unit, 0),
    GEODE_EQ(Eq25, "EQ_25", "25 Hz", 0), GEODE_EQ(Eq40, "EQ_40", "40 Hz", 1), GEODE_EQ(Eq60, "EQ_60", "60 Hz", 2),
    GEODE_EQ(Eq90, "EQ_90", "90 Hz", 3), GEODE_EQ(Eq150, "EQ_150", "150 Hz", 4), GEODE_EQ(Eq240, "EQ_240", "240 Hz", 5),
    GEODE_EQ(Eq370, "EQ_370", "370 Hz", 6), GEODE_EQ(Eq590, "EQ_590", "590 Hz", 7), GEODE_EQ(Eq930, "EQ_930", "930 Hz", 8),
    GEODE_EQ(Eq1k5, "EQ_1K5", "1.5 kHz", 9), GEODE_EQ(Eq2k3, "EQ_2K3", "2.3 kHz", 10), GEODE_EQ(Eq3k6, "EQ_3K6", "3.6 kHz", 11),
    GEODE_EQ(Eq5k6, "EQ_5K6", "5.6 kHz", 12), GEODE_EQ(Eq8k9, "EQ_8K9", "8.9 kHz", 13), GEODE_EQ(Eq14k, "EQ_14K", "14 kHz", 14),
    GEODE_EQ(Eq22k, "EQ_22K", "22 kHz", 15),
}};

#undef GEODE_SRC
#undef GEODE_EQ

using P = SceneParams;
constexpr float kOpen = 0.0f;  // min/max of a wrapping target: unused

#define GEODE_TGT(t, name, label, group, field, scale, lo, hi, lum) \
    TargetRow{LfoTarget::t, name, label, group, &P::field, scale, lo, hi, lum, false}
#define GEODE_SLOT(t, name, label) TargetRow{LfoTarget::t, name, label, "SLOTS", nullptr, 0, 0, 0, false, false}

// The columns: one per LfoTarget, in its order. Scale and range are what the old applyLfoTarget switch
// did; a route's ±1 moves the field by `scale`.
constexpr std::array<TargetRow, kTargetCount> kTargets = {{
    TargetRow{LfoTarget::None, "NONE", "None", "NONE", nullptr, 0, 0, 0, false, false},
    GEODE_TGT(Speed, "SPEED", "Speed", "MOTION", speed, 1.0f, 0.05f, 4.0f, false),
    GEODE_TGT(Zoom, "ZOOM", "Zoom", "MOTION", zoom, 1.0f, 0.3f, 3.0f, false),
    GEODE_TGT(Rotation, "ROTATION", "Rotation", "MOTION", rotation, 1.5f, -3.0f, 3.0f, false),
    GEODE_TGT(Sway, "SWAY", "Sway", "MOTION", sway, 1.0f, 0.0f, 1.0f, false),
    GEODE_TGT(Pulse, "PULSE", "Pulse", "MOTION", pulse, 1.0f, 0.0f, 1.0f, false),
    GEODE_TGT(DriftX, "DRIFT_X", "Drift X", "MOTION", driftX, 1.0f, -1.0f, 1.0f, false),
    GEODE_TGT(DriftY, "DRIFT_Y", "Drift Y", "MOTION", driftY, 1.0f, -1.0f, 1.0f, false),
    GEODE_TGT(Warp, "WARP", "Warp", "SHAPE", warp, 1.0f, 0.0f, 1.0f, false),
    GEODE_TGT(Ripple, "RIPPLE", "Ripple", "SHAPE", ripple, 1.0f, 0.0f, 1.0f, false),
    GEODE_TGT(Morph, "MORPH", "Morph", "SHAPE", morph, 1.0f, 0.0f, 1.0f, false),
    GEODE_TGT(Twist, "TWIST", "Twist", "SHAPE", twist, 1.0f, -1.0f, 1.0f, false),
    GEODE_TGT(Tile, "TILE", "Tile", "SHAPE", tile, 2.0f, 1.0f, 6.0f, false),
    GEODE_TGT(Pixelate, "PIXELATE", "Pixelate", "FX", pixelate, 1.0f, 0.0f, 1.0f, false),
    GEODE_TGT(Posterize, "POSTERIZE", "Posterize", "FX", posterize, 1.0f, 0.0f, 1.0f, false),
    TargetRow{LfoTarget::ColorShift, "COLOR_SHIFT", "Hue shift", "COLOUR", &P::colorShift, 0.5f, kOpen, kOpen, true, true},
    GEODE_TGT(PaletteMix, "PALETTE_MIX", "Palette blend", "COLOUR", paletteMix, 1.0f, 0.0f, 1.0f, true),
    GEODE_TGT(Saturation, "SATURATION", "Saturation", "COLOUR", saturation, 1.0f, 0.0f, 1.5f, true),
    GEODE_TGT(Brightness, "BRIGHTNESS", "Brightness", "COLOUR", brightness, 1.0f, 0.2f, 2.0f, true),
    GEODE_TGT(Intensity, "INTENSITY", "Intensity", "COLOUR", intensity, 1.0f, 0.2f, 2.0f, true),
    GEODE_TGT(Bloom, "BLOOM", "Bloom", "FX", bloom, 1.0f, 0.0f, 1.0f, true),
    GEODE_TGT(Temperature, "TEMPERATURE", "Temperature", "COLOUR", temperature, 1.0f, -1.0f, 1.0f, true),
    GEODE_TGT(Turbulence, "TURBULENCE", "Turbulence", "MOTION", turbulence, 1.0f, 0.0f, 1.5f, false),
    GEODE_TGT(ChromaAb, "CHROMA_AB", "Chroma AB", "FX", chromaAb, 1.0f, 0.0f, 1.0f, false),
    GEODE_TGT(Vignette, "VIGNETTE", "Vignette", "FX", vignette, 1.0f, 0.0f, 1.0f, true),
    GEODE_TGT(Glitch, "GLITCH", "Glitch", "FX", glitch, 1.0f, 0.0f, 1.0f, true),
    GEODE_TGT(Fisheye, "FISHEYE", "Fisheye", "FX", fisheye, 1.0f, -1.0f, 1.0f, false),
    GEODE_TGT(ParticleSize, "PARTICLE_SIZE", "Particle size", "SHAPE", particleSize, 1.0f, 0.3f, 2.5f, false),
    GEODE_TGT(TrailLength, "TRAIL_LENGTH", "Trail length", "FX", trailLength, 1.0f, 0.05f, 0.98f, false),
    GEODE_TGT(FluidCurl, "FLUID_CURL", "Fluid curl", "FLUID", fluidCurl, 25.0f, 0.0f, 50.0f, false),
    GEODE_TGT(FluidRadius, "FLUID_RADIUS", "Fluid splat radius", "FLUID", fluidSplatRadius, 0.15f, 0.02f, 0.4f, false),
    GEODE_TGT(FluidForce, "FLUID_FORCE", "Fluid splat force", "FLUID", fluidSplatForce, 1.5f, 0.0f, 3.0f, false),
    GEODE_TGT(FluidGlow, "FLUID_GLOW", "Fluid glow", "FLUID", fluidBloomIntensity, 1.0f, 0.1f, 2.0f, false),
    GEODE_TGT(FluidFade, "FLUID_FADE", "Fluid fade", "FLUID", fluidDensityDissipation, 1.5f, 0.0f, 4.0f, false),
    GEODE_TGT(FluidCatchPull, "FLUID_CATCH_PULL", "Catch pull", "FLUID", fluidCatchPull, 1.5f, 0.0f, 3.0f, false),
    GEODE_TGT(FluidCatchRadius, "FLUID_CATCH_RADIUS", "Catch radius", "FLUID", fluidCatchRadius, 0.12f, 0.03f, 0.3f, false),
    GEODE_TGT(FlowStrength, "FLOW_STRENGTH", "Flow strength", "FLUID", flowStrength, 1.0f, 0.0f, 1.0f, false),
    GEODE_TGT(WaterRipple, "WATER_RIPPLE", "Ripple amp", "FLUID", waterRippleStrength, 1.0f, 0.0f, 2.0f, false),
    GEODE_TGT(RippleOverlay, "RIPPLE_OVERLAY", "Ripple ovl", "FX", rippleOverlayStrength, 1.0f, 0.0f, 1.0f, false),
    GEODE_SLOT(Lfo1Rate, "LFO1_RATE", "Slot 1 rate"),
    GEODE_SLOT(Lfo1Depth, "LFO1_DEPTH", "Slot 1 depth"),
    GEODE_SLOT(Lfo2Rate, "LFO2_RATE", "Slot 2 rate"),
    GEODE_SLOT(Lfo2Depth, "LFO2_DEPTH", "Slot 2 depth"),
    GEODE_SLOT(Lfo3Rate, "LFO3_RATE", "Slot 3 rate"),
    GEODE_SLOT(Lfo3Depth, "LFO3_DEPTH", "Slot 3 depth"),
    GEODE_TGT(CamFly, "CAM_FLY", "Camera speed", "CAMERA", camFly, 1.0f, 0.0f, 2.5f, false),
    GEODE_TGT(CamOrbit, "CAM_ORBIT", "Camera swing", "CAMERA", camOrbit, 1.0f, 0.0f, 1.0f, false),
    GEODE_TGT(CamPush, "CAM_PUSH", "Camera push", "CAMERA", camPush, 1.0f, -1.0f, 1.0f, false),
    GEODE_TGT(CamYaw, "CAM_YAW", "Camera turn", "CAMERA", camYaw, 1.0f, -1.0f, 1.0f, false),
    GEODE_TGT(CamPitch, "CAM_PITCH", "Camera tilt", "CAMERA", camPitch, 1.0f, -1.0f, 1.0f, false),
    GEODE_TGT(CamRoll, "CAM_ROLL", "Camera roll", "CAMERA", camRoll, 1.0f, -1.0f, 1.0f, false),
    GEODE_TGT(CamFov, "CAM_FOV", "Camera FOV", "CAMERA", camFov, 1.0f, 0.5f, 2.0f, false),
    GEODE_TGT(CamShake, "CAM_SHAKE", "Camera shake", "CAMERA", camShake, 1.0f, 0.0f, 1.0f, false),
    GEODE_TGT(CamCrane, "CAM_CRANE", "Camera rise", "CAMERA", camCrane, 1.0f, -1.0f, 1.0f, false),
    GEODE_TGT(CamCut, "CAM_CUT", "Camera cut", "CAMERA", camCut, 1.0f, 0.0f, 1.0f, false),
    GEODE_TGT(Fall, "FALL", "Fall through", "LAYERS", fall, 1.0f, -1.0f, 1.0f, false),
    GEODE_TGT(Fractal, "FRACTAL", "Fractal growth", "LAYERS", fractal, 1.0f, 0.0f, 1.0f, false),
    GEODE_TGT(Spawn, "SPAWN", "Spawn", "LAYERS", spawn, 1.0f, 0.0f, 1.0f, false),
    GEODE_TGT(CycleSpeed, "CYCLE_SPEED", "Colour cycle speed", "COLOUR", cycleSpeed, 0.3f, 0.0f, 0.6f, false),
    GEODE_TGT(DiveSpeed, "DIVE_SPEED", "Dive speed", "MOTION", endlessZoomSpeed, 0.6f, 0.0f, 1.2f, false),
    GEODE_TGT(Melt, "MELT", "Melt", "LAYERS", melt, 1.0f, 0.0f, 1.0f, false),
    GEODE_TGT(Freeze, "FREEZE", "Freeze", "LAYERS", freeze, 1.0f, 0.0f, 1.0f, false),
    GEODE_TGT(Shatter, "SHATTER", "Shatter", "LAYERS", shatter, 1.0f, 0.0f, 1.0f, false),
    GEODE_TGT(Particles, "PARTICLES", "Particles", "PARTICLES", particles, 1.0f, 0.0f, 1.0f, false),
    GEODE_TGT(ParticleFall, "PARTICLE_FALL", "Particle fall", "PARTICLES", particleFall, 1.0f, -1.0f, 1.0f, false),
}};

#undef GEODE_TGT
#undef GEODE_SLOT

constexpr bool sourcesInOrder() {
    for (size_t i = 0; i < kSources.size(); ++i) {
        if (static_cast<size_t>(kSources[i].id) != i) return false;
    }
    return true;
}
constexpr bool targetsInOrder() {
    for (size_t i = 0; i < kTargets.size(); ++i) {
        if (static_cast<size_t>(kTargets[i].id) != i) return false;
    }
    return true;
}
static_assert(sourcesInOrder(), "kSources must list every ReactSource in enum order");
static_assert(targetsInOrder(), "kTargets must list every LfoTarget in enum order");

bool isCounted(ReactSource s) {
    switch (s) {
        case ReactSource::Kick4: case ReactSource::Kick8: case ReactSource::Kick16: case ReactSource::Snare4:
            return true;
        default:
            return false;
    }
}

}  // namespace

const SourceRow& source(ReactSource s) {
    return kSources[static_cast<size_t>(std::clamp(static_cast<int>(s), 0, kReactSourceCount - 1))];
}

const TargetRow& target(LfoTarget t) {
    return kTargets[static_cast<size_t>(std::clamp(static_cast<int>(t), 0, kTargetCount - 1))];
}

const std::array<SourceRow, kReactSourceCount>& sources() { return kSources; }
const std::array<TargetRow, kTargetCount>& targets() { return kTargets; }

SmartRoute smart(ReactSource s) {
    const SourceRow& row = source(s);
    switch (row.kind) {
        case K::Clock:
            return {ReactMode::Spin, 0.2f, 0.2f};
        case K::Trigger:
            // Structure (a big moment, every bar or phrase) lands somewhere new and stays; a single hit
            // pulses and lets go over its own envelope.
            if (s == ReactSource::HardCut || isCounted(s)) return {ReactMode::Jump, 0.25f, 0.6f};
            return {ReactMode::Follow, 0.01f, row.scale};
        case K::Signed:
            return {ReactMode::Follow, 0.2f, 0.4f};
        case K::Relative:
            return {ReactMode::Follow, 0.03f, 0.25f};
        case K::Unit:
            break;
    }
    // Unit sources: the drum envelopes already carry their decay, the slow trends want seconds.
    if (std::strcmp(row.group, "DRUMS") == 0) return {ReactMode::Follow, 0.01f, 0.08f};
    switch (s) {
        case ReactSource::BassPresence: case ReactSource::MidPresence: case ReactSource::TreblePresence:
        case ReactSource::Intensity: case ReactSource::Presence: case ReactSource::EnergyTrend:
        case ReactSource::DensitySlow:
            return {ReactMode::Follow, 0.6f, 1.5f};
        case ReactSource::PitchClass: case ReactSource::PitchHeight: case ReactSource::NoteRandom:
            return {ReactMode::Follow, 0.15f, 0.3f};
        default:
            return {ReactMode::Follow, 0.08f, 0.35f};
    }
}

ReactRoute cell(ReactSource s, LfoTarget t, float amount) {
    const SmartRoute m = smart(s);
    return {true, s, t, m.mode, std::clamp(amount, -2.0f, 2.0f), ModCurve::Linear, m.attackSeconds, m.releaseSeconds};
}

namespace {

constexpr std::array<DriveRow, kDriveCount> kDrives = {{
    {LfoTarget::Speed, ReactSource::MidTime},
    {LfoTarget::Rotation, ReactSource::BassTime},
    {LfoTarget::Sway, ReactSource::TrebleTime},
    {LfoTarget::DriftX, ReactSource::HitTime},
    {LfoTarget::DriftY, ReactSource::HitTime},
    {LfoTarget::CamFly, ReactSource::CurvedTime},
    {LfoTarget::CycleSpeed, ReactSource::NoteTime},
    {LfoTarget::DiveSpeed, ReactSource::CurvedTime},
}};

constexpr float kMaxGate = 2.5f;

}  // namespace

const std::array<DriveRow, kDriveCount>& drives() { return kDrives; }

bool defaultDrive(LfoTarget t, ReactSource& out) {
    for (const DriveRow& d : kDrives) {
        if (d.target == t) {
            out = d.source;
            return true;
        }
    }
    return false;
}

SceneParams drive(const SceneParams& p, const ReactSignals& s, const std::array<ReactRoute, ReactionEngine::kRoutes>& routes) {
    SceneParams o = p;
    // Silence stills everything, whatever the source says.
    const float presence = s.value(ReactSource::Presence);
    for (const DriveRow& d : kDrives) {
        float gate = 0.0f;
        bool routed = false;
        for (const ReactRoute& r : routes) {
            if (!r.enabled || r.mode != ReactMode::Drive || r.target != d.target) continue;
            gate += 2.0f * shape(std::fabs(s.value(r.source)), r.curve) * r.amount;
            routed = true;
        }
        if (!routed) gate = 2.0f * s.value(d.source);
        gate = std::clamp(gate * presence, 0.0f, kMaxGate);
        const TargetRow& row = target(d.target);
        const float lo = std::min(row.min, 0.0f);
        o.*row.field = std::clamp(p.*row.field * gate, lo, row.max);
    }
    return o;
}

std::string catalogueText() {
    std::string text;
    for (const SourceRow& s : kSources) {
        text += "S|";
        text += s.name;
        text += '|';
        text += s.label;
        text += '|';
        text += s.group;
        text += s.event() ? "|1\n" : "|0\n";
    }
    for (const TargetRow& t : kTargets) {
        text += "T|";
        text += t.name;
        text += '|';
        text += t.label;
        text += '|';
        text += t.group;
        text += t.luminance ? "|1\n" : "|0\n";
    }
    return text;
}

}  // namespace geode::viz::matrix
