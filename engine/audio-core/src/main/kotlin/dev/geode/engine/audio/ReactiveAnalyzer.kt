package dev.geode.engine.audio

import dev.geode.engine.bridge.FeatureFrameLayout
import dev.geode.engine.bridge.GeodeNative

class ReactiveAnalyzer(
    val bandCount: Int = FeatureFrameLayout.BAND_COUNT,
    val fftSize: Int = 2048,
    sampleRateHz: Int = 48_000,
    private val hopRateHz: Float = 62.5f,
) : AutoCloseable {
    init {
        require(bandCount == FeatureFrameLayout.BAND_COUNT) {
            "native analysis produces ${FeatureFrameLayout.BAND_COUNT} bands, asked for $bandCount"
        }
        check(GeodeNative.featureFrameFloats() == FeatureFrameLayout.FLOATS) {
            "GeodeFeatureFrame has ${GeodeNative.featureFrameFloats()} floats, FeatureFrameLayout expects ${FeatureFrameLayout.FLOATS}"
        }
    }

    private var handle: Long = GeodeNative.analysisCreate(sampleRateHz, fftSize, hopRateHz)
    private val frame = FloatArray(FeatureFrameLayout.FLOATS).also { blank(it) }

    val bands: FloatArray = FloatArray(bandCount)
    val waveform: FloatArray = FloatArray(FeatureFrameLayout.WAVEFORM_POINTS)
    val chroma: FloatArray = FloatArray(FeatureFrameLayout.CHROMA_BINS)

    var sampleRateHz: Int = sampleRateHz
        set(value) {
            if (value != field) {
                field = value
                GeodeNative.analysisSetSampleRate(handle, value)
            }
        }

    @Volatile
    var attackSeconds: Float = 0.02f
        set(value) {
            field = value
            pushTuning()
        }

    @Volatile
    var releaseSeconds: Float = 0.15f
        set(value) {
            field = value
            pushTuning()
        }

    var sensitivity: Float = 3f
        set(value) {
            field = value
            pushTuning()
        }

    var refractoryMs: Float = 60f
        set(value) {
            field = value
            pushTuning()
        }

    private fun pushTuning() {
        GeodeNative.analysisSetTuning(handle, sensitivity, refractoryMs, attackSeconds, releaseSeconds)
    }

    val rms: Float get() = frame[FeatureFrameLayout.RMS]
    val bass: Float get() = frame[FeatureFrameLayout.BASS]
    val mid: Float get() = frame[FeatureFrameLayout.MID]
    val treble: Float get() = frame[FeatureFrameLayout.TREBLE]
    val centroid: Float get() = frame[FeatureFrameLayout.CENTROID]
    val fluxValue: Float get() = frame[FeatureFrameLayout.FLUX]
    val onset: Float get() = frame[FeatureFrameLayout.ONSET]
    val beat: Boolean get() = frame[FeatureFrameLayout.BEAT] > 0f
    val beatStrength: Float get() = frame[FeatureFrameLayout.BEAT_STRENGTH]
    val transient: Float get() = frame[FeatureFrameLayout.TRANSIENT]
    val beatPhase: Float get() = frame[FeatureFrameLayout.BEAT_PHASE]
    val pulseConfidence: Float get() = frame[FeatureFrameLayout.PULSE_CONFIDENCE]
    val bpm: Float get() = frame[FeatureFrameLayout.BPM]
    val tempoStability: Float get() = frame[FeatureFrameLayout.TEMPO_STABILITY]
    val barPhase: Float get() = frame[FeatureFrameLayout.BAR_PHASE]
    val beatInBar: Int get() = frame[FeatureFrameLayout.BEAT_IN_BAR].toInt()
    val downbeat: Boolean get() = frame[FeatureFrameLayout.DOWNBEAT] > 0f
    val downbeatConfidence: Float get() = frame[FeatureFrameLayout.DOWNBEAT_CONFIDENCE]
    val macroEnergy: Float get() = frame[FeatureFrameLayout.MACRO_ENERGY]
    val kick: Float get() = frame[FeatureFrameLayout.KICK]
    val snare: Float get() = frame[FeatureFrameLayout.SNARE]
    val hat: Float get() = frame[FeatureFrameLayout.HAT]
    val novelty: Float get() = frame[FeatureFrameLayout.NOVELTY]
    val sectionBoundary: Boolean get() = frame[FeatureFrameLayout.SECTION_BOUNDARY] > 0f
    val buildup: Float get() = frame[FeatureFrameLayout.BUILDUP]
    val drop: Boolean get() = frame[FeatureFrameLayout.DROP] > 0f
    val arrival: Boolean get() = frame[FeatureFrameLayout.ARRIVAL] > 0f
    val harmonicity: Float get() = frame[FeatureFrameLayout.HARMONICITY]
    val warmup: Float get() = frame[FeatureFrameLayout.WARMUP]
    val stereoWidth: Float get() = frame[FeatureFrameLayout.STEREO_WIDTH]
    val stereoCorrelation: Float get() = frame[FeatureFrameLayout.STEREO_CORRELATION]
    val stereoPan: Float get() = frame[FeatureFrameLayout.STEREO_PAN]
    val chromaConfidence: Float get() = frame[FeatureFrameLayout.CHROMA_CONFIDENCE]

    fun analyze(
        samples: FloatArray,
        dtSeconds: Float,
    ) = analyze(samples, null, dtSeconds)

    fun analyze(
        mid: FloatArray,
        side: FloatArray?,
        dtSeconds: Float,
    ) {
        require(mid.size >= fftSize) { "need $fftSize samples, got ${mid.size}" }
        GeodeNative.analysisAnalyze(handle, mid, side, dtSeconds, frame)
        unpack()
    }

    fun push(
        interleaved: FloatArray,
        frames: Int,
        channels: Int,
    ) {
        GeodeNative.analysisPush(handle, interleaved, frames, channels)
    }

    fun pull(): Boolean {
        if (!GeodeNative.analysisPull(handle, frame)) return false
        unpack()
        return true
    }

    fun key(): String = GeodeNative.analysisKey(handle)

    fun reset() {
        GeodeNative.analysisReset(handle)
        blank(frame)
        unpack()
    }

    override fun close() {
        if (handle != 0L) {
            GeodeNative.analysisDestroy(handle)
            handle = 0L
        }
    }

    private fun unpack() {
        frame.copyInto(bands, 0, FeatureFrameLayout.BANDS, FeatureFrameLayout.BANDS + bandCount)
        frame.copyInto(waveform, 0, FeatureFrameLayout.WAVEFORM, FeatureFrameLayout.WAVEFORM + waveform.size)
        frame.copyInto(chroma, 0, FeatureFrameLayout.CHROMA, FeatureFrameLayout.CHROMA + chroma.size)
    }

    private fun blank(target: FloatArray) {
        target.fill(0f)
        target[FeatureFrameLayout.STEREO_CORRELATION] = 1f
        target[FeatureFrameLayout.HARMONICITY] = UNDECIDED_HARMONICITY
    }

    companion object {
        const val UNDECIDED_HARMONICITY: Float = 0.5f
    }
}
