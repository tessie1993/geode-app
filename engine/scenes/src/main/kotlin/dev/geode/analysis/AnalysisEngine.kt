package dev.geode.analysis

import dev.geode.engine.audio.HopCursor
import dev.geode.engine.audio.ReactiveAnalyzer
import dev.geode.engine.audio.SampleRing
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import kotlinx.coroutines.yield
import kotlin.math.ceil

/**
 * Live analysis of whatever the [SampleRing] carries (playback tap, mic, other apps' audio).
 *
 * It runs the same native push/pull path as [OfflineAnalyzer], fed hop by hop from the ring by a
 * [HopCursor], so a live frame is computed from exactly the samples an export's frame is, and the
 * frame rate is the stream's (rate / hop), not a timer's. The loop only decides when to look.
 */
class AnalysisEngine(
    private val ring: SampleRing,
    val bandCount: Int = DEFAULT_BAND_COUNT,
) {
    /**
     * Held for every use of [analyzer]. The loop replaces the analyzer when the stream's rate
     * changes (every time constant inside is built for one hop rate), and the tuning setters and
     * [close] run on other threads. Uncontended outside a format change or a settings drag, and
     * the loop is a Default-dispatcher coroutine, not the audio callback.
     */
    private val analyzerLock = Any()

    private val rate = MutableStateFlow(0)

    /** The stream's rate as the tap reports it. Nothing is analysed until there is one. */
    var sampleRateHz: Int
        get() = rate.value
        set(value) {
            rate.value = value
        }

    /** Null until the first rate arrives: there is no default rate to analyse at. */
    private var analyzer: ReactiveAnalyzer? = null

    var attack: Float = DEFAULT_ATTACK
        set(value) {
            field = value
            synchronized(analyzerLock) { analyzer?.attackSeconds = BeatTuning.envelopeSeconds(value) }
        }

    var decay: Float = DEFAULT_DECAY
        set(value) {
            field = value
            synchronized(analyzerLock) { analyzer?.releaseSeconds = BeatTuning.envelopeSeconds(value) }
        }

    var beatSensitivity: Float = BeatTuning.SENSITIVITY_DEFAULT
        set(value) {
            field = BeatTuning.clampSensitivity(value)
            synchronized(analyzerLock) { analyzer?.sensitivity = field }
        }

    var beatMinIntervalMs: Float = BeatTuning.INTERVAL_MS_DEFAULT
        set(value) {
            field = BeatTuning.clampIntervalMs(value)
            synchronized(analyzerLock) { analyzer?.refractoryMs = field }
        }

    private val _features = MutableStateFlow(AudioFeatures.empty(bandCount))
    val features: StateFlow<AudioFeatures> = _features

    @Volatile
    private var resetPending = false

    fun reset() {
        resetPending = true
        _features.value = AudioFeatures.empty(bandCount)
    }

    /**
     * A one-hop pulse held for as long as a display frame can span.
     *
     * The analyser fires `beat`, `transient`, `kick`, `snare`, `hat`, `downbeat`, `sectionBoundary`,
     * `drop` and `arrival` for exactly one hop. [features] is a StateFlow, which conflates, and the
     * renderer reads it once per display frame, so at 30 fps (the thermal governor's paced rate)
     * most pulses would be lost before any scene saw them. Each pulse stays in the emitted frame for
     * [PULSE_HOLD_SECONDS] of audio, long enough for any consumer sampling at 21 Hz or better.
     * Consumers that must fire once per pulse edge-detect already (`live::Edge` in the fluid
     * emitters); the rest take a max-envelope, for which a held value is the same value.
     */
    private class PulseHold(
        private val holdHops: Int,
    ) {
        var level = 0f
            private set
        private var hopsLeft = 0

        fun step(value: Float): Float {
            if (value > 0f) {
                level = maxOf(level, value)
                hopsLeft = holdHops
            } else if (hopsLeft > 0) {
                hopsLeft--
                if (hopsLeft == 0) level = 0f
            }
            return level
        }
    }

    /** One analyser's run: its cursor, its pulse holds, its scratch. Rebuilt on a rate change or a reset. */
    private inner class Pass(
        val analyzer: ReactiveAnalyzer,
    ) {
        val hopNanos: Long = analyzer.hopSamples * NANOS_PER_SECOND / analyzer.sampleRateHz
        private val cursor = HopCursor(analyzer.hopSamples, analyzer.fftSize)
        private val scratch = FloatArray(analyzer.fftSize * ring.channelCount)
        private val silence = FloatArray(analyzer.hopSamples)
        private val holdHops = ceil(PULSE_HOLD_SECONDS * analyzer.hopRateHz).toInt().coerceAtLeast(1)
        private val beat = PulseHold(holdHops)
        private val beatStrength = PulseHold(holdHops)
        private val transient = PulseHold(holdHops)
        private val kick = PulseHold(holdHops)
        private val snare = PulseHold(holdHops)
        private val hat = PulseHold(holdHops)
        private val downbeat = PulseHold(holdHops)
        private val sectionBoundary = PulseHold(holdHops)
        private val drop = PulseHold(holdHops)
        private val arrival = PulseHold(holdHops)

        // Caller holds analyzerLock.
        fun step() {
            val plan = cursor.plan(ring.writtenFrames, ring.epoch, ring.oldestAvailable)
            if (plan.silence) {
                analyzer.push(silence, silence.size, 1)
                drain()
                return
            }
            // The native session buffers a few windows at most, so feed a window at a time.
            val channels = ring.sourceChannelCount.coerceIn(1, ring.channelCount)
            var at = 0
            while (at < plan.frames) {
                val n = minOf(analyzer.fftSize, plan.frames - at)
                // Overwritten while we read (a stalled loop): drop the rest; the cursor resyncs.
                if (!ring.readInterleaved(plan.fromFrame + at, n, channels, scratch)) return
                analyzer.push(scratch, n, channels)
                drain()
                at += n
            }
        }

        private fun drain() {
            while (analyzer.pull()) publish()
        }

        private fun publish() {
            val a = analyzer
            _features.value =
                AudioFeatures(
                    bands = a.bands.copyOf(),
                    waveform = a.waveform.copyOf(),
                    rms = a.rms,
                    bass = a.bass,
                    mid = a.mid,
                    treble = a.treble,
                    onset = a.onset,
                    beat = beat.step(if (a.beat) 1f else 0f) > 0f,
                    bpm = a.bpm,
                    centroid = a.centroid,
                    flux = a.fluxValue,
                    beatStrength = beatStrength.step(a.beatStrength),
                    transient = transient.step(a.transient),
                    beatPhase = a.beatPhase,
                    pulseConfidence = a.pulseConfidence,
                    macroEnergy = a.macroEnergy,
                    kick = kick.step(a.kick),
                    snare = snare.step(a.snare),
                    hat = hat.step(a.hat),
                    chroma = a.chroma.copyOf(),
                    chromaConfidence = a.chromaConfidence,
                    stereoWidth = a.stereoWidth,
                    stereoCorrelation = a.stereoCorrelation,
                    stereoPan = a.stereoPan,
                    tempoStability = a.tempoStability,
                    barPhase = a.barPhase,
                    beatInBar = a.beatInBar,
                    downbeat = downbeat.step(if (a.downbeat) 1f else 0f) > 0f,
                    downbeatConfidence = a.downbeatConfidence,
                    novelty = a.novelty,
                    sectionBoundary = sectionBoundary.step(if (a.sectionBoundary) 1f else 0f) > 0f,
                    buildup = a.buildup,
                    drop = drop.step(if (a.drop) 1f else 0f) > 0f,
                    arrival = arrival.step(if (a.arrival) 1f else 0f) > 0f,
                    harmonicity = a.harmonicity,
                    warmup = a.warmup,
                )
        }
    }

    private var pass: Pass? = null

    /**
     * One wake: brings the analyser to the tap's rate, honours a pending reset, and feeds whatever
     * audio the cursor says is due. Returns the hop's length in nanoseconds, or null when there is
     * no rate yet or the engine is closed. Caller holds [analyzerLock].
     */
    private fun wake(): Long? {
        // Checked under the lock close() takes, so no hop can start on a destroyed handle.
        if (closed) return null
        val wanted = rate.value
        if (wanted <= 0) return null
        var current = pass
        if (current == null || current.analyzer.sampleRateHz != wanted) {
            analyzer?.close()
            val fresh =
                ReactiveAnalyzer(sampleRateHz = wanted, bandCount = bandCount).also {
                    it.attackSeconds = BeatTuning.envelopeSeconds(attack)
                    it.releaseSeconds = BeatTuning.envelopeSeconds(decay)
                    it.sensitivity = beatSensitivity
                    it.refractoryMs = beatMinIntervalMs
                }
            analyzer = fresh
            current = Pass(fresh)
            pass = current
            resetPending = false
        } else if (resetPending) {
            resetPending = false
            current.analyzer.reset()
            current = Pass(current.analyzer)
            pass = current
        }
        current.step()
        return current.hopNanos
    }

    // job is mutated from AudioBus.onInterestChanged, which can fire on whatever thread calls
    // AudioBus.addConsumer()/removeConsumer() - not necessarily the analysis scope's thread -
    // so every read/write of it is synchronised on jobLock.
    private val jobLock = Any()
    private var job: Job? = null

    // Set before close() cancels the loop's job, and checked by wake() under analyzerLock, which
    // close() also takes before destroying the native handle: a hop already in flight finishes
    // first, and none starts afterwards.
    @Volatile
    private var closed = false

    fun start(scope: CoroutineScope) {
        synchronized(jobLock) {
            if (job?.isActive == true) return
            job =
                scope.launch(Dispatchers.Default) {
                    var deadlineNs = System.nanoTime()
                    while (!closed) {
                        val hopNs = synchronized(analyzerLock) { wake() }
                        if (hopNs == null) {
                            // No rate yet: wait for the tap or a capture source to report one.
                            rate.first { it > 0 }
                            deadlineNs = System.nanoTime()
                            continue
                        }
                        // Wake about once per hop of audio. The cursor, not this clock, decides
                        // what is analysed: a late wake feeds the hops it owes and an early one
                        // finds nothing due, so falling behind just restarts the schedule.
                        deadlineNs += hopNs
                        val now = System.nanoTime()
                        if (deadlineNs < now) deadlineNs = now
                        val waitMs = (deadlineNs - now) / NANOS_PER_MILLI
                        // Never delay(0): it returns without suspending. yield() keeps the loop
                        // cancellable without sleeping through a hop.
                        if (waitMs > 0) delay(waitMs) else yield()
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

    /**
     * Best-effort teardown: the loop may still observe [closed] one wake late. Prefer
     * [closeAndJoin] where a suspend context is available.
     */
    fun close() {
        closed = true
        stop()
        release()
    }

    /**
     * Cancels the loop and waits for it to actually stop before destroying the native handle, so
     * no in-flight native call can race the destroy.
     */
    suspend fun closeAndJoin() {
        closed = true
        val current = synchronized(jobLock) { job.also { job = null } }
        current?.cancelAndJoin()
        release()
    }

    private fun release() {
        synchronized(analyzerLock) {
            analyzer?.close()
            analyzer = null
            pass = null
        }
    }

    companion object {
        private const val NANOS_PER_SECOND = 1_000_000_000L
        private const val NANOS_PER_MILLI = 1_000_000L

        // One 30 fps display frame (33 ms) plus scheduling jitter, in audio time.
        private const val PULSE_HOLD_SECONDS = 0.048f

        const val DEFAULT_BAND_COUNT = 64

        const val DEFAULT_ATTACK = 0.6f
        const val DEFAULT_DECAY = 0.12f
    }
}
