package dev.geode.analysis

import dev.geode.engine.audio.AnalysisFft
import dev.geode.engine.audio.MidSideWindow
import dev.geode.engine.audio.ReactiveAnalyzer
import dev.geode.engine.audio.SampleRing
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.yield

class AnalysisEngine(
    private val ring: SampleRing,
    val bandCount: Int = DEFAULT_BAND_COUNT,
) {
    /**
     * Held for every use of [analyzer]. The loop swaps the analyzer for one with a larger FFT
     * when the stream's rate needs it ([AnalysisFft.sizeFor]), and the tuning setters and
     * [close] run on other threads. Uncontended outside a format change or a settings drag, and
     * the loop is a Default-dispatcher coroutine, not the audio callback.
     */
    private val analyzerLock = Any()

    /** The stream's rate as the tap reports it. The loop applies it before its next hop. */
    @Volatile
    var sampleRateHz: Int = INITIAL_SAMPLE_RATE_HZ

    private var analyzer =
        ReactiveAnalyzer(
            bandCount = bandCount,
            fftSize = AnalysisFft.sizeFor(INITIAL_SAMPLE_RATE_HZ),
            sampleRateHz = INITIAL_SAMPLE_RATE_HZ,
            hopRateHz = HOP_RATE_HZ,
        )

    var attack: Float = DEFAULT_ATTACK
        set(value) {
            field = value
            synchronized(analyzerLock) { analyzer.attackSeconds = BeatTuning.envelopeSeconds(value) }
        }

    var decay: Float = DEFAULT_DECAY
        set(value) {
            field = value
            synchronized(analyzerLock) { analyzer.releaseSeconds = BeatTuning.envelopeSeconds(value) }
        }

    var beatSensitivity: Float = BeatTuning.SENSITIVITY_DEFAULT
        set(value) {
            field = BeatTuning.clampSensitivity(value)
            synchronized(analyzerLock) { analyzer.sensitivity = field }
        }

    var beatMinIntervalMs: Float = BeatTuning.INTERVAL_MS_DEFAULT
        set(value) {
            field = BeatTuning.clampIntervalMs(value)
            synchronized(analyzerLock) { analyzer.refractoryMs = field }
        }

    /**
     * Brings [analyzer] to the tap's current rate; caller holds [analyzerLock]. A rate that
     * needs a different FFT size gets a new analyzer carrying the current tuning, since the
     * native session's buffers and band map are sized at creation.
     */
    private fun syncAnalyzerToRate() {
        val rate = sampleRateHz
        if (rate <= 0 || rate == analyzer.sampleRateHz) return
        val fft = AnalysisFft.sizeFor(rate)
        if (fft == analyzer.fftSize) {
            analyzer.sampleRateHz = rate
            return
        }
        analyzer.close()
        analyzer =
            ReactiveAnalyzer(bandCount = bandCount, fftSize = fft, sampleRateHz = rate, hopRateHz = HOP_RATE_HZ).also {
                it.attackSeconds = BeatTuning.envelopeSeconds(attack)
                it.releaseSeconds = BeatTuning.envelopeSeconds(decay)
                it.sensitivity = beatSensitivity
                it.refractoryMs = beatMinIntervalMs
            }
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

    /**
     * A one-hop pulse held for the hops a display frame can span.
     *
     * The analyser fires `beat`, `transient`, `kick`, `snare`, `hat`, `downbeat`, `sectionBoundary`,
     * `drop` and `arrival` for exactly one 16 ms hop. [features] is a StateFlow, which conflates,
     * and the renderer reads it once per display frame, so at 30 fps (the thermal governor's
     * paced rate) every other pulse used to be lost before any scene saw it. Each pulse now
     * stays in the emitted frame for [PULSE_HOLD_HOPS] hops, long enough for any consumer
     * sampling at 20 Hz or better. Consumers that must fire once per pulse edge-detect already
     * (`live::Edge` in the fluid emitters); the rest take a max-envelope, for which a held
     * value is the same value.
     */
    private class PulseHold {
        var level = 0f
            private set
        private var hopsLeft = 0

        fun step(value: Float): Float {
            if (value > 0f) {
                level = maxOf(level, value)
                hopsLeft = PULSE_HOLD_HOPS
            } else if (hopsLeft > 0) {
                hopsLeft--
                if (hopsLeft == 0) level = 0f
            }
            return level
        }

        fun reset() {
            level = 0f
            hopsLeft = 0
        }
    }

    internal inner class Pass {
        private var window = MidSideWindow(ring, synchronized(analyzerLock) { analyzer.fftSize })
        private val beat = PulseHold()
        private val beatStrength = PulseHold()
        private val transient = PulseHold()
        private val kick = PulseHold()
        private val snare = PulseHold()
        private val hat = PulseHold()
        private val downbeat = PulseHold()
        private val sectionBoundary = PulseHold()
        private val drop = PulseHold()
        private val arrival = PulseHold()

        fun reset() {
            synchronized(analyzerLock) { analyzer.reset() }
            listOf(beat, beatStrength, transient, kick, snare, hat, downbeat, sectionBoundary, drop, arrival)
                .forEach(PulseHold::reset)
        }

        fun tick(): Boolean {
            synchronized(analyzerLock) {
                // Checked under the lock close() takes, so no hop can start on a destroyed handle.
                if (closed) return false
                syncAnalyzerToRate()
                if (window.mid.size != analyzer.fftSize) window = MidSideWindow(ring, analyzer.fftSize)
                if (!window.refresh()) return false
                analyzer.analyze(window.mid, window.side, DT_SECONDS)
                publish()
            }
            return true
        }

        // Caller holds analyzerLock.
        private fun publish() {
            _features.value =
                AudioFeatures(
                    bands = analyzer.bands.copyOf(),
                    waveform = analyzer.waveform.copyOf(),
                    rms = analyzer.rms,
                    bass = analyzer.bass,
                    mid = analyzer.mid,
                    treble = analyzer.treble,
                    onset = analyzer.onset,
                    beat = beat.step(if (analyzer.beat) 1f else 0f) > 0f,
                    bpm = analyzer.bpm,
                    centroid = analyzer.centroid,
                    flux = analyzer.fluxValue,
                    beatStrength = beatStrength.step(analyzer.beatStrength),
                    transient = transient.step(analyzer.transient),
                    beatPhase = analyzer.beatPhase,
                    pulseConfidence = analyzer.pulseConfidence,
                    macroEnergy = analyzer.macroEnergy,
                    kick = kick.step(analyzer.kick),
                    snare = snare.step(analyzer.snare),
                    hat = hat.step(analyzer.hat),
                    chroma = analyzer.chroma.copyOf(),
                    chromaConfidence = analyzer.chromaConfidence,
                    stereoWidth = analyzer.stereoWidth,
                    stereoCorrelation = analyzer.stereoCorrelation,
                    stereoPan = analyzer.stereoPan,
                    tempoStability = analyzer.tempoStability,
                    barPhase = analyzer.barPhase,
                    beatInBar = analyzer.beatInBar,
                    downbeat = downbeat.step(if (analyzer.downbeat) 1f else 0f) > 0f,
                    downbeatConfidence = analyzer.downbeatConfidence,
                    novelty = analyzer.novelty,
                    sectionBoundary = sectionBoundary.step(if (analyzer.sectionBoundary) 1f else 0f) > 0f,
                    buildup = analyzer.buildup,
                    drop = drop.step(if (analyzer.drop) 1f else 0f) > 0f,
                    arrival = arrival.step(if (analyzer.arrival) 1f else 0f) > 0f,
                    harmonicity = analyzer.harmonicity,
                    warmup = analyzer.warmup,
                )
        }
    }

    // job is mutated from AudioBus.onInterestChanged, which can fire on whatever thread calls
    // AudioBus.addConsumer()/removeConsumer() - not necessarily the analysis scope's thread -
    // so every read/write of it is synchronised on jobLock.
    private val jobLock = Any()
    private var job: Job? = null

    // Set before close() cancels the loop's job, and checked by pass.tick() under analyzerLock,
    // which close() also takes before destroying the native handle: a hop already in flight
    // finishes first, and none starts afterwards.
    @Volatile
    private var closed = false

    fun start(scope: CoroutineScope) {
        synchronized(jobLock) {
            if (job?.isActive == true) return
            job =
                scope.launch(Dispatchers.Default) {
                    val pass = Pass()
                    var deadlineNs = System.nanoTime()
                    while (!closed) {
                        if (resetPending) {
                            resetPending = false
                            pass.reset()
                        }
                        pass.tick()
                        deadlineNs += TICK_NS
                        val now = System.nanoTime()
                        // Behind schedule: run the missed hops back to back instead of skipping
                        // them. The tempo tracker counts hops, not seconds, so every dropped hop
                        // shortened the beat periods it spanned and the BPM read high whenever
                        // the loop was starved. Further behind than MAX_CATCH_UP_HOPS the loop
                        // was stopped rather than slow (a frozen process, a debugger), and the
                        // schedule restarts from now.
                        if (now - deadlineNs > MAX_CATCH_UP_HOPS * TICK_NS) deadlineNs = now
                        if (deadlineNs > now) {
                            delay(maxOf(1L, (deadlineNs - now) / 1_000_000))
                        } else {
                            // Never delay(0): it returns without suspending. yield() keeps a
                            // catch-up run cancellable without sleeping through the hop it owes.
                            yield()
                        }
                    }
                }
        }
    }

    fun stop() {
        synchronized(jobLock) {
            job?.cancel()
            job = null
        }
    }

    /** Best-effort teardown: the loop may still observe [closed] one tick late. Prefer [closeAndJoin] where a suspend context is available. */
    fun close() {
        closed = true
        stop()
        synchronized(analyzerLock) { analyzer.close() }
    }

    /** Cancels the loop and waits for it to actually stop before destroying the native handle, so no in-flight native call can race the destroy. */
    suspend fun closeAndJoin() {
        closed = true
        val current = synchronized(jobLock) { job.also { job = null } }
        current?.cancelAndJoin()
        synchronized(analyzerLock) { analyzer.close() }
    }

    companion object {
        private const val TICK_NS = 16_000_000L

        /** 128 ms of hops: past this the loop was suspended, not slow. */
        private const val MAX_CATCH_UP_HOPS = 8L

        /** Until the tap reports the stream's format; the loop rebuilds for the real rate. */
        private const val INITIAL_SAMPLE_RATE_HZ = 48_000

        // Three hops is 48 ms: one 30 fps display frame plus scheduling jitter.
        private const val PULSE_HOLD_HOPS = 3

        internal const val HOP_RATE_HZ = 1000f / 16f
        internal const val DT_SECONDS = 16f / 1000f

        const val DEFAULT_BAND_COUNT = 64

        const val DEFAULT_ATTACK = 0.6f
        const val DEFAULT_DECAY = 0.12f
    }
}
