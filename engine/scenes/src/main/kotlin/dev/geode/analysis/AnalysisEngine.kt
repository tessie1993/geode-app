package dev.geode.analysis

import dev.geode.engine.audio.MidSideWindow
import dev.geode.engine.audio.ReactiveAnalyzer
import dev.geode.engine.audio.SampleRing
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch

class AnalysisEngine(
    private val ring: SampleRing,
    val bandCount: Int = DEFAULT_BAND_COUNT,
    private val fftSize: Int = DEFAULT_FFT_SIZE,
) {
    private val analyzer =
        ReactiveAnalyzer(
            bandCount = bandCount,
            fftSize = fftSize,
            hopRateHz = HOP_RATE_HZ,
        )

    @Volatile
    var sampleRateHz: Int = 44100
        set(value) {
            field = value
            analyzer.sampleRateHz = value
        }

    var attack: Float = DEFAULT_ATTACK
        set(value) {
            field = value
            analyzer.attackSeconds = BeatTuning.envelopeSeconds(value)
        }

    var decay: Float = DEFAULT_DECAY
        set(value) {
            field = value
            analyzer.releaseSeconds = BeatTuning.envelopeSeconds(value)
        }

    var beatSensitivity: Float = BeatTuning.SENSITIVITY_DEFAULT
        set(value) {
            field = BeatTuning.clampSensitivity(value)
            analyzer.sensitivity = field
        }

    var beatMinIntervalMs: Float = BeatTuning.INTERVAL_MS_DEFAULT
        set(value) {
            field = BeatTuning.clampIntervalMs(value)
            analyzer.refractoryMs = field
        }

    private val _features = MutableStateFlow(AudioFeatures.empty(bandCount))
    val features: StateFlow<AudioFeatures> = _features

    @Volatile
    private var resetPending = false

    init {
        attack = DEFAULT_ATTACK
        decay = DEFAULT_DECAY
    }

    fun reset() {
        resetPending = true
        _features.value = AudioFeatures.empty(bandCount)
    }

    internal inner class Pass {
        private val window = MidSideWindow(ring, fftSize)

        fun reset() {
            analyzer.reset()
        }

        fun tick(): Boolean {
            if (!window.refresh()) return false
            analyzer.analyze(window.mid, window.side, DT_SECONDS)

            _features.value =
                AudioFeatures(
                    bands = analyzer.bands.copyOf(),
                    waveform = analyzer.waveform.copyOf(),
                    rms = analyzer.rms,
                    bass = analyzer.bass,
                    mid = analyzer.mid,
                    treble = analyzer.treble,
                    onset = analyzer.onset,
                    beat = analyzer.beat,
                    bpm = analyzer.bpm,
                    centroid = analyzer.centroid,
                    flux = analyzer.fluxValue,
                    beatStrength = analyzer.beatStrength,
                    transient = analyzer.transient,
                    beatPhase = analyzer.beatPhase,
                    pulseConfidence = analyzer.pulseConfidence,
                    macroEnergy = analyzer.macroEnergy,
                    kick = analyzer.kick,
                    snare = analyzer.snare,
                    hat = analyzer.hat,
                    chroma = analyzer.chroma.copyOf(),
                    chromaConfidence = analyzer.chromaConfidence,
                    stereoWidth = analyzer.stereoWidth,
                    stereoCorrelation = analyzer.stereoCorrelation,
                    stereoPan = analyzer.stereoPan,
                )
            return true
        }
    }

    private var job: Job? = null

    fun start(scope: CoroutineScope) {
        if (job?.isActive == true) return
        job =
            scope.launch(Dispatchers.Default) {
                val pass = Pass()
                var deadlineNs = System.nanoTime()
                while (true) {
                    if (resetPending) {
                        resetPending = false
                        pass.reset()
                    }
                    pass.tick()
                    deadlineNs += TICK_NS
                    val now = System.nanoTime()
                    if (deadlineNs < now) deadlineNs = now
                    // Never delay(0): it returns without suspending, so a tick that
                    // overruns the budget would leave this loop with no suspension
                    // point at all - uncancellable, and spinning a core flat out.
                    delay(maxOf(1L, (deadlineNs - now) / 1_000_000))
                }
            }
    }

    fun stop() {
        job?.cancel()
        job = null
    }

    fun close() {
        stop()
        analyzer.close()
    }

    companion object {
        private const val TICK_NS = 16_000_000L

        internal const val HOP_RATE_HZ = 1000f / 16f
        internal const val DT_SECONDS = 16f / 1000f

        const val DEFAULT_BAND_COUNT = 64

        const val DEFAULT_FFT_SIZE = 2048

        const val DEFAULT_ATTACK = 0.6f
        const val DEFAULT_DECAY = 0.12f
    }
}
