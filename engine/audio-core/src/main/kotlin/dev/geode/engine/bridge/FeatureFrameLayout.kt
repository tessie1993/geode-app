package dev.geode.engine.bridge

/** Float offsets of `GeodeFeatureFrame` (core/api/geode_api.h); the order there is the order here. */
object FeatureFrameLayout {
    const val RMS = 0
    const val BASS = 1
    const val MID = 2
    const val TREBLE = 3
    const val CENTROID = 4
    const val FLUX = 5
    const val ONSET = 6
    const val BEAT = 7
    const val BEAT_STRENGTH = 8
    const val TRANSIENT = 9
    const val BEAT_PHASE = 10
    const val PULSE_CONFIDENCE = 11
    const val BPM = 12
    const val TEMPO_STABILITY = 13
    const val BAR_PHASE = 14
    const val BEAT_IN_BAR = 15
    const val DOWNBEAT = 16
    const val DOWNBEAT_CONFIDENCE = 17
    const val MACRO_ENERGY = 18
    const val KICK = 19
    const val SNARE = 20
    const val HAT = 21
    const val NOVELTY = 22
    const val SECTION_BOUNDARY = 23
    const val BUILDUP = 24
    const val DROP = 25
    const val ARRIVAL = 26
    const val HARMONICITY = 27
    const val WARMUP = 28
    const val STEREO_WIDTH = 29
    const val STEREO_CORRELATION = 30
    const val STEREO_PAN = 31
    const val CHROMA_CONFIDENCE = 32
    const val BAND_COUNT = 64
    const val WAVEFORM_POINTS = 128
    const val CHROMA_BINS = 12
    const val BANDS = 33
    const val WAVEFORM = BANDS + BAND_COUNT
    const val CHROMA = WAVEFORM + WAVEFORM_POINTS
    const val FLOATS = CHROMA + CHROMA_BINS
}
