package dev.geode.analysis

data class AudioFeatures(
    val bands: FloatArray,
    val waveform: FloatArray,
    val rms: Float = 0f,
    val bass: Float = 0f,
    val mid: Float = 0f,
    val treble: Float = 0f,
    val onset: Float = 0f,
    val beat: Boolean = false,
    val bpm: Float = 0f,
    val centroid: Float = 0f,
    val progress: Float = 0f,
    val sectionIndex: Int = 0,
    val sectionCount: Int = 0,
    val flux: Float = 0f,
    val beatStrength: Float = 0f,
    val beatPhase: Float = 0f,
    val transient: Float = 0f,
    val pulseConfidence: Float = 0f,
    val macroEnergy: Float = 0f,
    val kick: Float = 0f,
    val snare: Float = 0f,
    val hat: Float = 0f,
    val stereoWidth: Float = 0f,
    val stereoCorrelation: Float = 1f,
    val stereoPan: Float = 0f,
    val chroma: FloatArray = EMPTY_CHROMA,
    val chromaConfidence: Float = 0f,
    // The musical-structure half of the native frame. The analyser has always
    // computed these; until they were carried here FeatureFrameCodec zero-filled
    // them, so no scene ever saw a drop, a section, a bar or a buildup.
    val tempoStability: Float = 0f,
    val barPhase: Float = 0f,
    val beatInBar: Int = 0,
    val downbeat: Boolean = false,
    val downbeatConfidence: Float = 0f,
    val novelty: Float = 0f,
    val sectionBoundary: Boolean = false,
    val buildup: Float = 0f,
    val drop: Boolean = false,
    val arrival: Boolean = false,
    val harmonicity: Float = HARMONICITY_UNDECIDED,
    val warmup: Float = 0f,
) {
    val hasChroma: Boolean get() = chroma.size == 12

    val beatImpulse: Float
        get() =
            when {
                !beat -> 0f
                beatStrength > 0f -> beatStrength
                else -> 1f
            }

    val motionImpulse: Float
        get() = maxOf(beatImpulse, transient * TRANSIENT_MOTION_WEIGHT)

    companion object {
        const val TRANSIENT_MOTION_WEIGHT = 0.5f

        /** What the native HarmonicBalance reports before it has decided (its kUndecided). */
        const val HARMONICITY_UNDECIDED = 0.5f

        val EMPTY_CHROMA = FloatArray(0)

        fun empty(
            bandCount: Int = 64,
            waveformSize: Int = 128,
        ): AudioFeatures = AudioFeatures(FloatArray(bandCount), FloatArray(waveformSize))
    }
}
