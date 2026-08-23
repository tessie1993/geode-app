package dev.geode.engine.audio

import kotlin.math.exp
import kotlin.math.max
import kotlin.math.sqrt

class StructureTracker(
    private val bandCount: Int,
    private val hopRateHz: Float,
) {
    init {
        require(bandCount > 0) { "bandCount must be positive, was $bandCount" }
        require(hopRateHz > 0f) { "hopRateHz must be positive, was $hopRateHz" }
    }

    private val dt = 1f / hopRateHz

    private fun poleFor(seconds: Float) = 1f - exp(-1f / (seconds * hopRateHz))

    private val fastPole = poleFor(0.5f)
    private val slowPole = poleFor(8f)
    private val statsPole = poleFor(10f)
    private val fastEnergyPole = poleFor(1f)
    private val slowEnergyPole = poleFor(6f)
    private val noveltySmoothPole = poleFor(0.25f)

    private val fast = FloatArray(bandCount)
    private val slow = FloatArray(bandCount)
    private var noveltyPeak = NOVELTY_PEAK_FLOOR
    private var noveltyMean = 0f
    private var noveltyDev = 0f
    private var sectionArmed = true
    private var sinceSection = Float.MAX_VALUE
    private var fastEnergy = 0f
    private var slowEnergy = 0f
    private var energySeeded = false
    private var buildupMemory = 0f
    private var sinceDip = Float.MAX_VALUE
    private var dipSeconds = 0f
    private var sinceDrop = Float.MAX_VALUE
    private var quietSeconds = 0f
    private var arrivalArmed = false
    private var warmupSeconds = 0f

    var novelty: Float = 0f
        private set

    var sectionBoundary: Boolean = false
        private set

    var sectionCount: Int = 0
        private set

    var buildup: Float = 0f
        private set

    var drop: Boolean = false
        private set

    var arrival: Boolean = false
        private set

    fun step(
        bands: FloatArray,
        rms: Float,
        onset: Float,
    ) {
        require(bands.size == bandCount) { "expected $bandCount bands, got ${bands.size}" }
        warmupSeconds += dt
        sinceSection += dt
        sinceDip += dt
        sinceDrop += dt

        var distance = 0.0
        for (b in 0 until bandCount) {
            val v = bands[b]
            fast[b] += (v - fast[b]) * fastPole
            slow[b] += (v - slow[b]) * slowPole
            val d = fast[b] - slow[b]
            distance += d.toDouble() * d
        }
        val raw = sqrt(distance / bandCount).toFloat()
        noveltyPeak = max(raw, max(noveltyPeak * PEAK_DECAY, NOVELTY_PEAK_FLOOR))
        val normalized = (raw / noveltyPeak).coerceIn(0f, 1f)
        novelty += (normalized - novelty) * noveltySmoothPole

        noveltyMean += (novelty - noveltyMean) * statsPole
        noveltyDev += (kotlin.math.abs(novelty - noveltyMean) - noveltyDev) * statsPole
        val threshold = max(SECTION_FLOOR, noveltyMean + 2f * noveltyDev)
        sectionBoundary = false
        if (sectionArmed && warmupSeconds > WARMUP_SECONDS &&
            sinceSection > SECTION_REFRACTORY_SECONDS && novelty > threshold
        ) {
            sectionBoundary = true
            sectionCount++
            sectionArmed = false
            sinceSection = 0f
        } else if (!sectionArmed && novelty < SECTION_REARM) {
            sectionArmed = true
        }

        val energy = 0.5f * rms + 0.5f * onset
        if (!energySeeded) {
            energySeeded = true
            fastEnergy = energy
            slowEnergy = energy
        }
        fastEnergy += (energy - fastEnergy) * fastEnergyPole
        slowEnergy += (energy - slowEnergy) * slowEnergyPole
        buildup = ((fastEnergy - slowEnergy) / BUILDUP_SCALE).coerceIn(0f, 1f)
        buildupMemory = max(buildup, buildupMemory * exp(-dt / BUILDUP_MEMORY_SECONDS))

        if (rms < slowEnergy * DIP_FRACTION) {
            dipSeconds += dt
            if (dipSeconds >= MIN_DIP_SECONDS) sinceDip = 0f
        } else {
            dipSeconds = 0f
        }

        drop = false
        if (warmupSeconds > WARMUP_SECONDS &&
            sinceDrop > DROP_REFRACTORY_SECONDS &&
            buildupMemory > DROP_BUILDUP &&
            sinceDip < DROP_DIP_WINDOW_SECONDS &&
            rms > DROP_SLAM_LEVEL &&
            rms > slowEnergy + DROP_SLAM_MARGIN
        ) {
            drop = true
            sinceDrop = 0f
            buildupMemory = 0f
        }

        arrival = false
        if (rms < ARRIVAL_QUIET_LEVEL) {
            quietSeconds += dt
            if (quietSeconds >= ARRIVAL_QUIET_SECONDS) arrivalArmed = true
        } else {
            if (arrivalArmed && rms > ARRIVAL_RECOVERY_LEVEL && warmupSeconds > WARMUP_SECONDS) {
                arrival = true
                arrivalArmed = false
            }
            quietSeconds = 0f
        }
    }

    fun reset() {
        fast.fill(0f)
        slow.fill(0f)
        noveltyPeak = NOVELTY_PEAK_FLOOR
        noveltyMean = 0f
        noveltyDev = 0f
        sectionArmed = true
        sinceSection = Float.MAX_VALUE
        fastEnergy = 0f
        slowEnergy = 0f
        energySeeded = false
        buildupMemory = 0f
        sinceDip = Float.MAX_VALUE
        dipSeconds = 0f
        sinceDrop = Float.MAX_VALUE
        quietSeconds = 0f
        arrivalArmed = false
        warmupSeconds = 0f
        novelty = 0f
        sectionBoundary = false
        sectionCount = 0
        buildup = 0f
        drop = false
        arrival = false
    }

    companion object {
        const val WARMUP_SECONDS = 5f

        const val SECTION_FLOOR = 0.5f

        const val SECTION_REARM = 0.35f

        const val SECTION_REFRACTORY_SECONDS = 8f

        const val PEAK_DECAY = 0.9997f

        const val NOVELTY_PEAK_FLOOR = 0.05f

        const val BUILDUP_SCALE = 0.3f

        const val BUILDUP_MEMORY_SECONDS = 3f

        const val DROP_BUILDUP = 0.4f
        const val DROP_DIP_WINDOW_SECONDS = 1f
        const val DROP_SLAM_LEVEL = 0.75f
        const val DROP_SLAM_MARGIN = 0.15f
        const val DROP_REFRACTORY_SECONDS = 4f

        const val DIP_FRACTION = 0.35f

        const val MIN_DIP_SECONDS = 0.1f

        const val ARRIVAL_QUIET_LEVEL = 0.15f
        const val ARRIVAL_QUIET_SECONDS = 2f
        const val ARRIVAL_RECOVERY_LEVEL = 0.4f
    }
}
