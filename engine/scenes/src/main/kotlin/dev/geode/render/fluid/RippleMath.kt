package dev.geode.render.fluid

import kotlin.math.cos
import kotlin.math.exp
import kotlin.math.sin
import kotlin.math.sqrt

internal object RippleMath {
    const val REFRACTION_CAP = 0.08f

    const val MAX_HEIGHT = 8f

    const val HEIGHT_DECAY_RATIO = 0.35f

    fun heightDecayPerSubstep(
        damping: Float,
        subDt: Float,
    ): Float {
        val per60 = 1f - (1f - damping.coerceIn(0f, 1f)) * HEIGHT_DECAY_RATIO
        return Math.pow(per60.toDouble(), (subDt * 60f).toDouble()).toFloat()
    }

    fun waveStep(
        h: FloatArray,
        v: FloatArray,
        w: Int,
        hgt: Int,
        c: Float,
        dt: Float,
        dx: Float,
        damping: Float,
        heightDecay: Float = 1f,
    ) {
        val k = c * c * dt / (dx * dx)
        for (y in 0 until hgt) {
            for (x in 0 until w) {
                val i = y * w + x
                val l = h[y * w + (x - 1).coerceAtLeast(0)]
                val r = h[y * w + (x + 1).coerceAtMost(w - 1)]
                val b = h[(y - 1).coerceAtLeast(0) * w + x]
                val t = h[(y + 1).coerceAtMost(hgt - 1) * w + x]
                v[i] = (v[i] + k * (l + r + t + b - 4f * h[i])) * damping
            }
        }
        for (i in h.indices) {
            h[i] = ((h[i] + v[i] * dt) * heightDecay).coerceIn(-MAX_HEIGHT, MAX_HEIGHT)
        }
    }

    fun dropProfile(
        dist: Float,
        radius: Float,
        amp: Float,
    ): Float {
        val r = maxOf(radius, 1e-4f)
        return amp * exp(-(dist * dist) / (r * r))
    }

    fun cflClampedDt(
        c: Float,
        dt: Float,
        dx: Float,
    ): Float {
        if (c <= 1e-6f) return dt
        return minOf(dt, 0.7f * dx / c)
    }

    fun refractionOffset(
        hL: Float,
        hR: Float,
        hT: Float,
        hB: Float,
        strength: Float,
    ): Pair<Float, Float> {
        var ox = (hR - hL) * strength
        var oy = (hT - hB) * strength
        val len = sqrt(ox * ox + oy * oy)
        val k = REFRACTION_CAP / (REFRACTION_CAP + len)
        ox *= k
        oy *= k
        return ox to oy
    }

    fun inkDissipation(
        dissipation: Float,
        dt: Float,
    ): Float = (1f - dissipation.coerceIn(0f, 8f) * dt).coerceIn(0f, 1f)

    data class StrokeDrop(
        val x: Float,
        val y: Float,
        val radius: Float,
        val amplitude: Float,
    )

    private const val STROKE_REFERENCE_SPEED = 1.6f

    fun strokeDrops(
        x: Float,
        y: Float,
        dx: Float,
        dy: Float,
        dt: Float,
        radius: Float,
        strength: Float,
    ): List<StrokeDrop> {
        val step = sqrt(dx * dx + dy * dy)
        val speed = if (dt > 1e-4f) step / dt else 0f
        val drive = (0.25f + (speed / STROKE_REFERENCE_SPEED).coerceIn(0f, 1.5f)) * strength.coerceIn(0f, 2f)
        if (drive <= 1e-4f) return emptyList()
        val ux = if (step > 1e-5f) dx / step else 0f
        val uy = if (step > 1e-5f) dy / step else 0f
        val lead = radius * 0.6f
        val crest = StrokeDrop(x + ux * lead, y + uy * lead, radius, drive)
        if (step <= 1e-5f) return listOf(crest)
        return listOf(crest, StrokeDrop(x - ux * lead, y - uy * lead, radius, -drive * 0.8f))
    }

    private const val GOLDEN_ANGLE = 2.3999631f

    private const val GOLDEN_FRACT = 0.6180339887f

    fun overlayDropPosition(
        index: Int,
        aspect: Float,
    ): Pair<Float, Float> {
        val n = index.coerceAtLeast(0)
        val angle = n * GOLDEN_ANGLE
        val radius = 0.85f * sqrt(((n * GOLDEN_FRACT.toDouble()) % 1.0).toFloat())
        return (cos(angle) * radius * aspect) to (sin(angle) * radius)
    }
}
