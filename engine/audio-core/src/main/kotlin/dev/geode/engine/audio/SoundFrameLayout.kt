package dev.geode.engine.audio

/**
 * Float offsets into a `GeodeSoundFrame` (`core/sound/SoundFrame.h`) as it crosses JNI: every field in
 * declaration order, then the EQ, spectrum and waveform arrays. `SoundFrameLayoutTest` parses the header,
 * so a field the engine adds, drops or moves fails CI here instead of shifting every reader by one.
 *
 * What each value means (relative, shaped, clock, trigger) is documented on the struct, not repeated here.
 */
@Suppress("unused")
object SoundFrameLayout {
    const val BASS = 0
    const val MID = 1
    const val TREBLE = 2
    const val VOL = 3
    const val BASS_ATT = 4
    const val MID_ATT = 5
    const val TREBLE_ATT = 6
    const val VOL_ATT = 7
    const val SUB = 8
    const val SUB_ATT = 9
    const val MID_HIGH = 10
    const val MID_HIGH_ATT = 11
    const val LOW = 12
    const val MIDDLE = 13
    const val HIGH = 14
    const val LEVEL = 15
    const val BASS_PRESENCE = 16
    const val MID_PRESENCE = 17
    const val TREBLE_PRESENCE = 18
    const val INTENSITY = 19
    const val BASS_TIME = 20
    const val MID_TIME = 21
    const val TREBLE_TIME = 22
    const val CURVED_TIME = 23
    const val HIT_TIME = 24
    const val NOTE_TIME = 25
    const val PRESENCE = 26
    const val RMS = 27
    const val LOUDNESS = 28
    const val CENTROID = 29
    const val ROLLOFF = 30
    const val FLATNESS = 31
    const val DENSITY_SLOW = 32
    const val DENSITY_FAST = 33
    const val FLUX = 34
    const val ENERGY_TREND = 35
    const val PEAK_FREQ = 36
    const val PEAK_MAG = 37
    const val STEREO_WIDTH = 38
    const val STEREO_PAN = 39
    const val KICK_ENV = 40
    const val SNARE_ENV = 41
    const val HAT_ENV = 42
    const val ONSET_ENV = 43
    const val KICK_TOGGLE = 44
    const val SNARE_TOGGLE = 45
    const val KICK_RANDOM = 46
    const val NOTE_RANDOM = 47
    const val KICK = 48
    const val SNARE = 49
    const val HAT = 50
    const val KICK2 = 51
    const val KICK4 = 52
    const val KICK8 = 53
    const val KICK16 = 54
    const val SNARE2 = 55
    const val SNARE4 = 56
    const val HARD_CUT = 57
    const val ONSET = 58
    const val SILENCE = 59
    const val RETURNED = 60
    const val SURGE = 61
    const val NOTE_CHANGE = 62
    const val LEGATO = 63
    const val REATTACK = 64
    const val PITCH = 65
    const val PITCH_CLASS = 66
    const val PITCH_HEIGHT = 67
    const val PITCH_CONFIDENCE = 68
    const val INTERVAL = 69
    const val GLIDE = 70
    const val VIBRATO = 71
    const val EVENNESS = 72
    const val RUN_DENSITY = 73

    const val SCALARS = 74
    const val EQ_BANDS = 16
    const val SPECTRUM_POINTS = 64
    const val WAVEFORM_POINTS = 128
    const val EQ = SCALARS
    const val SPECTRUM = EQ + EQ_BANDS
    const val WAVEFORM = SPECTRUM + SPECTRUM_POINTS
    const val FLOATS = WAVEFORM + WAVEFORM_POINTS

    /** `GeodeSoundTuning` as floats: gain, threshold, offset, attack, release, drum, drumDecay (3 each), then 8 scalars. */
    const val TUNING_FLOATS = 29

    /** Field names in order, for the header test and for debug overlays. */
    val SCALAR_NAMES: List<String> =
        listOf(
            "bass",
            "mid",
            "treble",
            "vol",
            "bassAtt",
            "midAtt",
            "trebleAtt",
            "volAtt",
            "sub",
            "subAtt",
            "midHigh",
            "midHighAtt",
            "low",
            "middle",
            "high",
            "level",
            "bassPresence",
            "midPresence",
            "treblePresence",
            "intensity",
            "bassTime",
            "midTime",
            "trebleTime",
            "curvedTime",
            "hitTime",
            "noteTime",
            "presence",
            "rms",
            "loudness",
            "centroid",
            "rolloff",
            "flatness",
            "densitySlow",
            "densityFast",
            "flux",
            "energyTrend",
            "peakFreq",
            "peakMag",
            "stereoWidth",
            "stereoPan",
            "kickEnv",
            "snareEnv",
            "hatEnv",
            "onsetEnv",
            "kickToggle",
            "snareToggle",
            "kickRandom",
            "noteRandom",
            "kick",
            "snare",
            "hat",
            "kick2",
            "kick4",
            "kick8",
            "kick16",
            "snare2",
            "snare4",
            "hardCut",
            "onset",
            "silence",
            "returned",
            "surge",
            "noteChange",
            "legato",
            "reattack",
            "pitch",
            "pitchClass",
            "pitchHeight",
            "pitchConfidence",
            "interval",
            "glide",
            "vibrato",
            "evenness",
            "runDensity",
        )
}
