package dev.geode.render.scene

import kotlin.math.PI

class CymaticsPlate {
    private val amplitudes = FloatArray(CymaticsMath.MODES.size)

    private val taken = BooleanArray(CymaticsMath.MODES.size)

    private val excitation = FloatArray(CymaticsMath.MODES.size)

    private var map: IntArray = IntArray(0)
    private var mapBandCount = -1
    private var mapFundamental = Float.NaN

    private var smoothed = FloatArray(0)

    private val phases = FloatArray(CymaticsMath.MODES.size)

    var dominantWavenumber: Float = 0f
        private set

    fun reset() {
        amplitudes.fill(0f)
        excitation.fill(0f)
        phases.fill(0f)
        dominantWavenumber = 0f
    }

    fun excite(
        bands: FloatArray,
        dt: Float,
        fundamentalHz: Float,
        drive: Float,
        ringSeconds: Float,
        focus: Float,
    ) {
        if (bands.isEmpty() || dt <= 0f) return
        ensureMap(bands.size, fundamentalHz)
        ensureSmoothing(bands.size)
        val f = focus.coerceIn(0f, 1f)
        localMean(bands)
        excitation.fill(0f)
        for (b in bands.indices) {
            val raw = bands[b].coerceAtLeast(0f)
            val peak = (raw - smoothed[b]).coerceAtLeast(0f) * WHITEN_GAIN
            val value = (raw * (1f - f) + peak * f) * drive
            val mode = map[b]
            if (value > excitation[mode]) excitation[mode] = value
        }
        val attack = CymaticsMath.smoothing(dt, CymaticsMath.ATTACK_SECONDS)
        val release = CymaticsMath.smoothing(dt, ringSeconds)
        var loudest = 0f
        var loudestIndex = -1
        for (i in amplitudes.indices) {
            val target = excitation[i]
            val a = amplitudes[i]
            var next = a + (target - a) * (if (target > a) attack else release)
            if (next < CymaticsMath.SILENCE) next = 0f
            amplitudes[i] = next
            if (next > loudest) {
                loudest = next
                loudestIndex = i
            }
        }
        dominantWavenumber = if (loudestIndex >= 0) CymaticsMath.MODES[loudestIndex].wavenumber else 0f
    }

    fun advancePhases(
        dt: Float,
        speed: Float,
    ) {
        if (dt <= 0f) return
        val rate = speed.coerceIn(0.05f, 4f) * TWO_PI * dt
        for (i in phases.indices) {
            if (amplitudes[i] <= CymaticsMath.SILENCE) continue
            phases[i] = (phases[i] + CymaticsMath.vibrationHz(CymaticsMath.MODES[i].wavenumber) * rate) % TWO_PI
        }
    }

    fun snapshot(
        limit: Int,
        out: FloatArray,
    ): Int {
        val want = limit.coerceIn(1, CymaticsMath.MAX_RENDERED_MODES).coerceAtMost(out.size / 4)
        var written = 0
        var total = 0f
        taken.fill(false)
        repeat(want) {
            var bestIndex = -1
            var best = 0f
            for (i in amplitudes.indices) {
                if (taken[i]) continue
                if (amplitudes[i] > best) {
                    best = amplitudes[i]
                    bestIndex = i
                }
            }
            if (bestIndex < 0 || best <= CymaticsMath.SILENCE) return@repeat
            taken[bestIndex] = true
            val mode = CymaticsMath.MODES[bestIndex]
            val base = written * 4
            out[base] = mode.n.toFloat()
            out[base + 1] = mode.m.toFloat()
            out[base + 2] = best
            out[base + 3] = phases[bestIndex]
            total += best
            written++
        }
        if (written == 0) return 0
        val norm = 1f / maxOf(1f, total)
        for (i in 0 until written) out[i * 4 + 2] *= norm
        return written
    }

    val ringing: Boolean
        get() = amplitudes.any { it > CymaticsMath.SILENCE }

    private fun ensureMap(
        bandCount: Int,
        fundamentalHz: Float,
    ) {
        val f0 = fundamentalHz.coerceIn(CymaticsMath.MIN_FUNDAMENTAL_HZ, CymaticsMath.MAX_FUNDAMENTAL_HZ)
        if (bandCount == mapBandCount && f0 == mapFundamental) return
        map = CymaticsMath.bandModeMap(bandCount, f0)
        mapBandCount = bandCount
        mapFundamental = f0
    }

    private fun ensureSmoothing(bandCount: Int) {
        if (smoothed.size != bandCount) smoothed = FloatArray(bandCount)
    }

    private fun localMean(bands: FloatArray) {
        val r = CymaticsMath.WHITEN_RADIUS
        for (b in bands.indices) {
            var sum = 0f
            var n = 0
            for (k in (b - r)..(b + r)) {
                if (k < 0 || k >= bands.size) continue
                sum += bands[k].coerceAtLeast(0f)
                n++
            }
            smoothed[b] = if (n > 0) sum / n else 0f
        }
    }

    private companion object {
        const val TWO_PI = 2f * PI.toFloat()

        const val WHITEN_GAIN = 2.6f
    }
}
