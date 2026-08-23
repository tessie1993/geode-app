package dev.geode.render.fluid

import dev.geode.analysis.AudioFeatures
import kotlin.math.sqrt

internal object FluidMath {
    const val MIN_AUDIO_DRIVE = 0.2f
    const val MAX_AUDIO_DRIVE = 2.5f

    const val DRIVE_CEILING = 1.5f

    fun driven(
        value: Float,
        audioDrive: Float,
    ): Float {
        val d = audioDrive.coerceIn(MIN_AUDIO_DRIVE, MAX_AUDIO_DRIVE)
        return (value * d).coerceIn(0f, maxOf(value, DRIVE_CEILING))
    }

    private fun fract(x: Float) = x - kotlin.math.floor(x)

    private fun hash3(
        x0: Float,
        y0: Float,
        z0: Float,
    ): Float {
        var x = fract(x0 * 0.3183099f + 0.1f) * 17f
        var y = fract(y0 * 0.3183099f + 0.2f) * 17f
        var z = fract(z0 * 0.3183099f + 0.3f) * 17f
        return fract(x * y * z * (x + y + z))
    }

    private fun vnoise3(
        px: Float,
        py: Float,
        pz: Float,
    ): Float {
        val ix = kotlin.math.floor(px)
        val iy = kotlin.math.floor(py)
        val iz = kotlin.math.floor(pz)
        var fx = px - ix
        var fy = py - iy
        var fz = pz - iz
        fx = fx * fx * (3f - 2f * fx)
        fy = fy * fy * (3f - 2f * fy)
        fz = fz * fz * (3f - 2f * fz)

        fun n(
            dx: Float,
            dy: Float,
            dz: Float,
        ) = hash3(ix + dx, iy + dy, iz + dz)

        fun mix(
            a: Float,
            b: Float,
            t: Float,
        ) = a + (b - a) * t
        return mix(
            mix(mix(n(0f, 0f, 0f), n(1f, 0f, 0f), fx), mix(n(0f, 1f, 0f), n(1f, 1f, 0f), fx), fy),
            mix(mix(n(0f, 0f, 1f), n(1f, 0f, 1f), fx), mix(n(0f, 1f, 1f), n(1f, 1f, 1f), fx), fy),
            fz,
        )
    }

    private fun psi(
        x: Float,
        y: Float,
        time: Float,
        freq: Float,
        detail: Float,
    ): Float {
        var v = vnoise3(x * freq, y * freq, time) * 0.625f
        v += vnoise3(x * freq * 2.02f + 11.3f, y * freq * 2.02f + 11.3f, time * 2.02f + 11.3f) * 0.25f
        v += vnoise3(x * freq * 4.05f + 29.7f, y * freq * 4.05f + 29.7f, time * 4.05f + 29.7f) * 0.125f * detail
        return v
    }

    fun curlVelocity(
        x: Float,
        y: Float,
        time: Float,
        freq: Float,
        detail: Float,
    ): Pair<Float, Float> {
        val e = 0.02f
        val dpdx = psi(x + e, y, time, freq, detail) - psi(x - e, y, time, freq, detail)
        val dpdy = psi(x, y + e, time, freq, detail) - psi(x, y - e, time, freq, detail)
        return (dpdy / (2f * e)) to (-dpdx / (2f * e))
    }

    fun confinementDeltaV(
        curlStrength: Float,
        dx: Float,
        velDiff: Float,
        dt: Float,
    ): Float {
        val omega = (0.5f / dx) * velDiff
        return curlStrength * dx * omega * dt
    }

    fun softLimitFlow(
        x: Float,
        y: Float,
    ): Pair<Float, Float> {
        val len = sqrt(x * x + y * y)
        val k = 6f / (6f + len)
        return (x * k) to (y * k)
    }

    fun terminalSpeedCap(
        x: Float,
        y: Float,
    ): Pair<Float, Float> {
        val sp = sqrt(x * x + y * y)
        val k = 12f / maxOf(12f, sp)
        return (x * k) to (y * k)
    }

    fun stateSide(count: Int): Int =
        kotlin.math
            .ceil(kotlin.math.sqrt(count.toDouble()))
            .toInt()
            .coerceAtLeast(2)

    fun attractorForce(
        pull: Float,
        dist2: Float,
    ): Float {
        val f = pull / (dist2 + 0.05f)
        return f * 6f / (6f + f)
    }

    fun isCaptured(
        px: Float,
        py: Float,
        cx: Float,
        cy: Float,
        captureRadius: Float,
    ): Boolean {
        val dx = cx - px
        val dy = cy - py
        return dx * dx + dy * dy < captureRadius * captureRadius
    }

    fun segDist(
        ax: Float,
        ay: Float,
        bx: Float,
        by: Float,
        px: Float,
        py: Float,
    ): Pair<Float, Float> {
        val abx = bx - ax
        val aby = by - ay
        val len2 = abx * abx + aby * aby
        if (len2 < 1e-8f) {
            val dx = px - ax
            val dy = py - ay
            return sqrt(dx * dx + dy * dy) to 0f
        }
        val fp = (((px - ax) * abx + (py - ay) * aby) / len2).coerceIn(0f, 1f)
        val cx = ax + abx * fp
        val cy = ay + aby * fp
        val dx = px - cx
        val dy = py - cy
        return sqrt(dx * dx + dy * dy) to fp
    }

    fun dragStep(
        v: Float,
        flow: Float,
        drag: Float,
        dt: Float = 1f / 60f,
    ): Float {
        val k = 1f - Math.pow((1f - drag).toDouble(), (dt * 60f).toDouble()).toFloat()
        return v + (flow - v) * k
    }

    fun bloomCurve(
        threshold: Float,
        softKnee: Float,
    ): Triple<Float, Float, Float> {
        val knee = threshold * softKnee + 1e-4f
        return Triple(threshold - knee, knee * 2f, 0.25f / knee)
    }

    fun bloomPrefilterScale(
        br: Float,
        threshold: Float,
        softKnee: Float,
    ): Float {
        val (cx, cy, cz) = bloomCurve(threshold, softKnee)
        var rq = (br - cx).coerceIn(0f, cy)
        rq = cz * rq * rq
        return maxOf(rq, br - threshold) / maxOf(br, 1e-4f)
    }
}

internal class FluidAudioDrive {
    private var bands = FloatArray(0)

    fun scaled(
        features: AudioFeatures,
        audioDrive: Float,
    ): AudioFeatures {
        val d = audioDrive.coerceIn(FluidMath.MIN_AUDIO_DRIVE, FluidMath.MAX_AUDIO_DRIVE)
        if (d == 1f) return features
        if (bands.size != features.bands.size) bands = FloatArray(features.bands.size)
        for (i in features.bands.indices) bands[i] = FluidMath.driven(features.bands[i], d)
        return features.copy(
            bands = bands,
            rms = FluidMath.driven(features.rms, d),
            bass = FluidMath.driven(features.bass, d),
            mid = FluidMath.driven(features.mid, d),
            treble = FluidMath.driven(features.treble, d),
        )
    }
}
