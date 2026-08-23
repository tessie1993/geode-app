package dev.geode.render.scene

import dev.geode.engine.audio.LogBands
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.atan2
import kotlin.math.cos
import kotlin.math.exp
import kotlin.math.ln
import kotlin.math.sin
import kotlin.math.sqrt

object CymaticsMath {
    const val MIN_BAND_HZ: Float = LogBands.DEFAULT_MIN_HZ

    const val MAX_BAND_HZ: Float = LogBands.DEFAULT_MAX_HZ

    const val MAX_ORDER: Int = 14

    const val MAX_RENDERED_MODES: Int = 8

    const val MIN_FUNDAMENTAL_HZ: Float = 40f
    const val MAX_FUNDAMENTAL_HZ: Float = 440f

    const val ATTACK_SECONDS: Float = 0.035f

    const val MIN_RING_SECONDS: Float = 0.06f
    const val MAX_RING_SECONDS: Float = 2.5f

    const val SILENCE: Float = 1e-4f

    const val WHITEN_RADIUS: Int = 4

    const val BESSEL_GAIN: Float = 1.7f

    const val VIBRATION_HZ_PER_ORDER: Float = 0.14f
    const val MIN_VIBRATION_HZ: Float = 0.12f
    const val MAX_VIBRATION_HZ: Float = 1.6f

    data class Mode(
        val n: Int,
        val m: Int,
    ) {
        val wavenumber: Float = sqrt((n * n + m * m).toFloat())
    }

    val MODES: List<Mode> =
        buildList {
            for (n in 1..MAX_ORDER) {
                for (m in 0 until n) add(Mode(n, m))
            }
        }.sortedBy { it.wavenumber }

    private val WAVENUMBERS: FloatArray = FloatArray(MODES.size) { MODES[it].wavenumber }

    fun bandCenterHz(
        band: Int,
        bandCount: Int,
    ): Float {
        if (bandCount <= 0) return MIN_BAND_HZ
        val ratio = ln(MAX_BAND_HZ / MIN_BAND_HZ)
        val low = MIN_BAND_HZ * exp(ratio * band / bandCount).toFloat()
        val high = MIN_BAND_HZ * exp(ratio * (band + 1) / bandCount).toFloat()
        return sqrt(low * high)
    }

    fun wavenumberFor(
        hz: Float,
        fundamentalHz: Float,
    ): Float {
        val f0 = fundamentalHz.coerceIn(MIN_FUNDAMENTAL_HZ, MAX_FUNDAMENTAL_HZ)
        return sqrt((hz / f0).coerceAtLeast(0f))
    }

    fun modeIndexFor(wavenumber: Float): Int {
        var best = 0
        var bestDelta = Float.MAX_VALUE
        for (i in WAVENUMBERS.indices) {
            val delta = abs(WAVENUMBERS[i] - wavenumber)
            if (delta > bestDelta) break
            best = i
            bestDelta = delta
        }
        return best
    }

    fun bandModeMap(
        bandCount: Int,
        fundamentalHz: Float,
    ): IntArray =
        IntArray(bandCount) { band ->
            modeIndexFor(wavenumberFor(bandCenterHz(band, bandCount), fundamentalHz))
        }

    fun modeHeight(
        n: Int,
        m: Int,
        x: Float,
        y: Float,
    ): Float {
        val pi = PI.toFloat()
        val nx = n * pi * x
        val my = m * pi * y
        val mx = m * pi * x
        val ny = n * pi * y
        return cos(nx) * cos(my) - cos(mx) * cos(ny)
    }

    fun angularOrder(mode: Mode): Int = mode.m

    fun radialOrder(mode: Mode): Int = maxOf(mode.n - mode.m, 1)

    fun dishBeta(mode: Mode): Float = PI.toFloat() * (radialOrder(mode) + 0.5f * angularOrder(mode) - 0.25f)

    fun besselApprox(
        m: Float,
        x: Float,
        phase: Float = 0f,
    ): Float {
        val ax = abs(x)
        val pi = PI.toFloat()
        val core = if (m < 0.5f) 1f else ax * ax / (ax * ax + 0.45f * m * m + 0.05f)
        val w = x - m * pi * 0.5f - pi * 0.25f - phase
        val inv = 1f / (8f * maxOf(ax, 0.75f))
        val mu = 4f * m * m
        val c1 = ((mu - 1f) * inv).coerceIn(-3f, 3f)
        val c0 = (1f - (mu - 1f) * (mu - 9f) * inv * inv * 0.5f).coerceIn(-3f, 3f)
        return (c0 * cos(w) - c1 * sin(w)) / sqrt(1f + 2f * ax) * core * BESSEL_GAIN
    }

    fun dishHeight(
        modes: FloatArray,
        count: Int,
        x: Float,
        y: Float,
        travel: Float = 0f,
    ): Float {
        val r = sqrt(x * x + y * y)
        val a = atan2(y, x)
        var h = 0f
        for (i in 0 until count) {
            val base = i * 4
            val mode = Mode(modes[base].toInt(), modes[base + 1].toInt())
            val ang = angularOrder(mode).toFloat()
            val beta = dishBeta(mode)
            h += modes[base + 2] * besselApprox(ang, beta * r, travel) * cos(ang * a + modes[base + 3])
        }
        return h
    }

    fun ringSeconds(ring: Float): Float = MIN_RING_SECONDS + (MAX_RING_SECONDS - MIN_RING_SECONDS) * ring.coerceIn(0f, 1f)

    fun smoothing(
        dt: Float,
        tau: Float,
    ): Float = if (tau <= 0f) 1f else (1f - exp(-dt / tau)).coerceIn(0f, 1f)

    fun vibrationHz(wavenumber: Float): Float = (VIBRATION_HZ_PER_ORDER * wavenumber).coerceIn(MIN_VIBRATION_HZ, MAX_VIBRATION_HZ)

    const val MAX_DRIVE: Float = 4f

    const val LIVE_AMPLITUDE: Float = 0.02f

    const val ROOM_MODES: Int = 4

    fun safeDrive(raw: Float): Float = if (raw.isFinite()) raw.coerceIn(0f, MAX_DRIVE) else 0f

    fun fieldLiveness(totalAmplitude: Float): Float =
        if (totalAmplitude.isFinite()) (totalAmplitude / LIVE_AMPLITUDE).coerceIn(0f, 1f) else 0f

    fun wrapPhase(
        value: Float,
        period: Float,
    ): Float {
        if (!value.isFinite() || period <= 0f) return 0f
        val r = value % period
        return if (r < 0f) r + period else r
    }

    fun approachHue(
        current: Float,
        target: Float,
        alpha: Float,
    ): Float {
        var d = (target - current) % 1f
        if (d > 0.5f) d -= 1f
        if (d < -0.5f) d += 1f
        val next = current + d * alpha.coerceIn(0f, 1f)
        return next - kotlin.math.floor(next)
    }

    fun hexLattice(
        x: Float,
        y: Float,
    ): Float {
        val s3 = 0.8660254f
        return (cos(x) + cos(-0.5f * x + s3 * y) + cos(-0.5f * x - s3 * y)) / 3f
    }

    fun roomModeHeight(
        modes: FloatArray,
        count: Int,
        x: Float,
        y: Float,
        drift: Float = 0f,
    ): Float {
        val pi = PI.toFloat()
        var h = 0f
        for (i in 0 until minOf(count, ROOM_MODES)) {
            val base = i * 4
            h += modes[base + 2] *
                cos(modes[base] * pi * (x + drift)) *
                cos(modes[base + 1] * pi * y) *
                cos(modes[base + 3])
        }
        return h
    }

    fun nodalGate(fwidthH: Float): Float = smoothstepf(2.0e-5f, 1.2e-4f, fwidthH)

    private fun smoothstepf(
        edge0: Float,
        edge1: Float,
        x: Float,
    ): Float {
        val t = ((x - edge0) / (edge1 - edge0)).coerceIn(0f, 1f)
        return t * t * (3f - 2f * t)
    }
}
