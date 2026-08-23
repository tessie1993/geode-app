package dev.geode.engine.audio

import kotlin.math.exp
import kotlin.math.max
import kotlin.math.sqrt

class ReactiveAnalyzer(
    val bandCount: Int = 64,
    val fftSize: Int = 2048,
    sampleRateHz: Int = 48_000,
    private val hopRateHz: Float = 62.5f,
) {
    private val window = WindowTable(fftSize, WindowShape.HANN)
    private val windowed = FloatArray(fftSize)
    private val spectrum = Spectrum(fftSize)
    private val logBands = LogBands(bandCount, fftSize, sampleRateHz)

    private val bandPower = FloatArray(bandCount)
    private val bandDb = FloatArray(bandCount)
    private val bandNormalized = FloatArray(bandCount)
    private val whitened = FloatArray(bandCount)

    private val range = AdaptiveRange(bandCount)
    private val whitening = AdaptiveWhitening(bandCount)
    private val flux = SuperFlux(bandCount)
    private val picker = OnsetPeakPicker(hopRateHz)
    private val tempo = TempoTracker(hopRateHz)
    private val grid = BeatGrid()
    private val stability = TempoStability(hopRateHz)
    private val bar = BarTracker()

    private var drums = DrumChannels(bandCount, hopRateHz, sampleRateHz)

    private val structure = StructureTracker(bandCount, hopRateHz)
    private val harmonic = HarmonicBalance(fftSize / 2 + 1, hopRateHz)

    val bands: FloatArray = FloatArray(bandCount)
    private val smoothingState = FloatArray(bandCount)

    var sampleRateHz: Int = sampleRateHz
        set(value) {
            if (value != field) {
                field = value
                logBands.sampleRateHz = value
                drums = DrumChannels(bandCount, hopRateHz, value).also { it.sensitivity = picker.sensitivity }
            }
        }

    @Volatile
    var attackSeconds: Float = 0.02f

    @Volatile
    var releaseSeconds: Float = 0.15f

    var sensitivity: Float
        get() = picker.sensitivity
        set(value) {
            picker.sensitivity = value
            drums.sensitivity = value
        }

    var refractoryMs: Float
        get() = picker.refractorySeconds * 1000f
        set(value) {
            picker.refractorySeconds = value / 1000f
        }

    var rms: Float = 0f
        private set

    var bass: Float = 0f
        private set

    var mid: Float = 0f
        private set

    var treble: Float = 0f
        private set

    var centroid: Float = 0f
        private set

    var fluxValue: Float = 0f
        private set

    var onset: Float = 0f
        private set

    var beat: Boolean = false
        private set

    var beatStrength: Float = 0f
        private set

    var transient: Float = 0f
        private set

    var beatPhase: Float = 0f
        private set

    var pulseConfidence: Float = 0f
        private set

    var bpm: Float = 0f
        private set

    var tempoStability: Float = 0f
        private set

    var barPhase: Float = 0f
        private set

    var beatInBar: Int = 0
        private set

    var downbeat: Boolean = false
        private set

    var downbeatConfidence: Float = 0f
        private set

    var macroEnergy: Float = 0f
        private set

    var kick: Float = 0f
        private set

    var snare: Float = 0f
        private set

    var hat: Float = 0f
        private set

    var novelty: Float = 0f
        private set

    var sectionBoundary: Boolean = false
        private set

    var buildup: Float = 0f
        private set

    var drop: Boolean = false
        private set

    var arrival: Boolean = false
        private set

    var harmonicity: Float = HarmonicBalance.UNDECIDED
        private set

    val warmup: Float get() = range.warmup

    private var levelPeak = 0f
    private var lastFrameSilent = true
    private var silentSeconds = 0f

    fun analyze(
        samples: FloatArray,
        dtSeconds: Float,
    ) {
        require(samples.size >= fftSize) { "need $fftSize samples, got ${samples.size}" }

        rms = rmsOf(samples)
        if (rms < SILENCE_RMS) {
            lastFrameSilent = true
            silentSeconds += dtSeconds
            if (silentSeconds > STABILITY_SILENCE_HOLD_SECONDS) {
                stability.step(0f)
                tempoStability = stability.value
            }
            silenceOutputs()
            return
        }

        lastFrameSilent = false
        silentSeconds = 0f
        window.applyInto(samples, 0, windowed)
        spectrum.compute(windowed)

        logBands.energy(spectrum.magnitudes, bandPower)
        toDb(bandPower, bandDb)
        range.normalize(bandDb, dtSeconds, bandNormalized)
        smooth(bandNormalized, dtSeconds)

        bass = mean(bands, 0, bandCount / 8)
        mid = mean(bands, bandCount / 8, bandCount / 2)
        treble = mean(bands, bandCount / 2, bandCount)
        centroid = normalizedCentroid()
        macroEnergy = macroEnergyOf(rms, dtSeconds)

        whitening.whiten(bandPower, dtSeconds, whitened)
        fluxValue = flux.next(whitened)
        val isOnset = picker.accept(fluxValue)
        onset = (fluxValue / max(picker.threshold * ONSET_HEADROOM, 1e-9f)).coerceIn(0f, 1f)
        transient = picker.strength

        tempo.step(fluxValue)
        bpm = tempo.bpm
        pulseConfidence = tempo.confidence
        stability.step(tempo.bpm)
        tempoStability = stability.value
        beat = grid.step(tempo.periodFrames, tempo.confidence, isOnset)
        beatPhase = grid.phase
        beatStrength = if (beat) picker.strength else 0f

        drums.step(whitened)
        kick = drums.kick
        snare = drums.snare
        hat = drums.hat

        bar.step(
            phase = grid.phase,
            beat = beat,
            locked = grid.locked,
            accent = if (beat) picker.strength + KICK_ACCENT_WEIGHT * drums.kick else 0f,
        )
        barPhase = bar.barPhase
        beatInBar = bar.beatInBar
        downbeat = bar.downbeat
        downbeatConfidence = bar.confidence

        structure.step(bands, rms, onset)
        novelty = structure.novelty
        sectionBoundary = structure.sectionBoundary
        buildup = structure.buildup
        drop = structure.drop
        arrival = structure.arrival
        harmonic.step(spectrum.magnitudes)
        harmonicity = harmonic.balance
    }

    fun reset() {
        range.reset()
        whitening.reset()
        flux.reset()
        picker.reset()
        tempo.reset()
        grid.reset()
        stability.reset()
        bar.reset()
        structure.reset()
        harmonic.reset()
        drums.reset()
        smoothingState.fill(0f)
        bands.fill(0f)
        levelPeak = 0f
        silentSeconds = 0f
        rms = 0f
        bass = 0f
        mid = 0f
        treble = 0f
        centroid = 0f
        fluxValue = 0f
        onset = 0f
        beat = false
        beatStrength = 0f
        transient = 0f
        beatPhase = 0f
        pulseConfidence = 0f
        bpm = 0f
        tempoStability = 0f
        barPhase = 0f
        beatInBar = 0
        downbeat = false
        downbeatConfidence = 0f
        novelty = 0f
        sectionBoundary = false
        buildup = 0f
        drop = false
        arrival = false
        harmonicity = HarmonicBalance.UNDECIDED
        macroEnergy = 0f
        kick = 0f
        snare = 0f
        hat = 0f
    }

    fun spectrumInto(out: FloatArray): FloatArray {
        require(out.size == fftSize / 2) { "expected ${fftSize / 2} bins, got ${out.size}" }
        if (lastFrameSilent) {
            out.fill(0f)
            return out
        }
        val scale = 2f / fftSize
        out[0] = 0f
        for (k in 1 until fftSize / 2) out[k] = spectrum.magnitudes[k] * scale
        return out
    }

    private fun silenceOutputs() {
        bands.fill(0f)
        smoothingState.fill(0f)
        bass = 0f
        mid = 0f
        treble = 0f
        centroid = 0f
        fluxValue = 0f
        onset = 0f
        beat = false
        beatStrength = 0f
        transient = 0f
        downbeat = false
        sectionBoundary = false
        drop = false
        arrival = false
        macroEnergy = 0f
        kick = 0f
        snare = 0f
        hat = 0f
    }

    private fun toDb(
        power: FloatArray,
        out: FloatArray,
    ) {
        for (b in 0 until bandCount) {
            val p = power[b]
            out[b] =
                if (p <= 0f) {
                    AdaptiveRange.SILENCE_DB
                } else {
                    max(10f * kotlin.math.log10(p), AdaptiveRange.SILENCE_DB)
                }
        }
    }

    private fun smooth(
        source: FloatArray,
        dtSeconds: Float,
    ) {
        if (dtSeconds <= 0f) {
            source.copyInto(bands)
            return
        }
        val attack = if (attackSeconds <= 0f) 1f else (1f - exp(-dtSeconds / attackSeconds)).coerceIn(0f, 1f)
        val release = if (releaseSeconds <= 0f) 1f else (1f - exp(-dtSeconds / releaseSeconds)).coerceIn(0f, 1f)
        for (b in 0 until bandCount) {
            val target = source[b]
            val k = if (target > smoothingState[b]) attack else release
            smoothingState[b] += (target - smoothingState[b]) * k
            bands[b] = smoothingState[b]
        }
    }

    private fun rmsOf(samples: FloatArray): Float {
        var acc = 0.0
        for (i in 0 until fftSize) {
            val v = samples[i]
            acc += v.toDouble() * v
        }
        return sqrt(acc / fftSize).toFloat().coerceIn(0f, 1f)
    }

    private fun normalizedCentroid(): Float {
        var weight = 0f
        var weighted = 0f
        for (b in 0 until bandCount) {
            weight += bands[b]
            weighted += bands[b] * b
        }
        return if (weight <= 1e-6f) 0f else (weighted / weight / (bandCount - 1)).coerceIn(0f, 1f)
    }

    private fun macroEnergyOf(
        level: Float,
        dtSeconds: Float,
    ): Float {
        val decay = if (dtSeconds <= 0f) 1f else exp(-dtSeconds / MACRO_PEAK_SECONDS)
        levelPeak = max(level, levelPeak * decay)
        return if (levelPeak <= 1e-6f) 0f else (level / levelPeak).coerceIn(0f, 1f)
    }

    private fun mean(
        values: FloatArray,
        from: Int,
        to: Int,
    ): Float {
        var acc = 0f
        for (i in from until to) acc += values[i]
        return acc / (to - from)
    }

    companion object {
        private const val ONSET_HEADROOM = 2f

        private const val MACRO_PEAK_SECONDS = 20f

        private const val KICK_ACCENT_WEIGHT = 0.5f

        private const val STABILITY_SILENCE_HOLD_SECONDS = 2f

        private const val SILENCE_RMS = 1e-5f
    }
}
