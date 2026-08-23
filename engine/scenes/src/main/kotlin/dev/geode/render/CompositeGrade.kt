package dev.geode.render

import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.floor
import kotlin.math.pow
import kotlin.math.sin

internal object CompositeGrade {
    enum class SceneFamily {
        SHADER,

        MILKDROP,

        FLUID,
    }

    data class Gate(
        val geo: Boolean,
        val mirrorInvert: Boolean,
        val grade: Boolean,
        val pulse: Boolean,
    ) {
        private val vec4: FloatArray =
            floatArrayOf(
                if (geo) 1f else 0f,
                if (mirrorInvert) 1f else 0f,
                if (grade) 1f else 0f,
                if (pulse) 1f else 0f,
            )

        fun toVec4(): FloatArray = vec4
    }

    private val GATES: Map<SceneFamily, Gate> =
        SceneFamily.entries.associateWith { family ->
            Gate(
                geo = family != SceneFamily.SHADER,
                mirrorInvert = family == SceneFamily.FLUID,
                grade = family == SceneFamily.FLUID,
                pulse = family == SceneFamily.MILKDROP || family == SceneFamily.FLUID,
            )
        }

    fun gateFor(family: SceneFamily): Gate = GATES.getValue(family)

    const val MIN_ZOOM: Float = 0.05f

    const val MIN_GAMMA: Float = 0.05f

    const val BEAT_DECAY: Float = 3f

    const val PULSE_GAIN: Float = 0.22f

    private const val LUMA_R: Float = 0.299f
    private const val LUMA_G: Float = 0.587f
    private const val LUMA_B: Float = 0.114f

    private const val TAU_GLSL: Float = 6.2831f

    private const val TAU: Float = 6.2831855f

    private const val AXIS: Float = 0.57735f

    fun integrateRotation(
        angle: Float,
        rotation: Float,
        dt: Float,
    ): Float = (angle + rotation * dt) % TAU

    fun integrateCyclePhase(
        phase: Float,
        cycleSpeed: Float,
        dt: Float,
        enabled: Boolean,
    ): Float = if (enabled) (phase + cycleSpeed * dt) % 1f else phase

    fun integrateBeatPulse(
        envelope: Float,
        impulse: Float,
        dt: Float,
    ): Float = maxOf(impulse, (envelope - dt * BEAT_DECAY)).coerceAtLeast(0f)

    fun integrateBeatPulse(
        envelope: Float,
        beat: Boolean,
        dt: Float,
    ): Float = integrateBeatPulse(envelope, if (beat) 1f else 0f, dt)

    fun pulseAmount(
        pulse: Float,
        envelope: Float,
    ): Float {
        val e = envelope.coerceIn(0f, 1f)
        return pulse.coerceIn(0f, 1f) * e * e
    }

    fun pulseScale(amount: Float): Float = 1f + amount.coerceAtLeast(0f) * PULSE_GAIN

    fun swayAngle(
        rotationAngle: Float,
        sway: Float,
        timeSeconds: Float,
    ): Float = if (abs(sway) > 1e-3f) rotationAngle + sway * 0.35f * sin(timeSeconds * 0.7f) else rotationAngle

    fun brightness(
        brightness: Float,
        intensity: Float,
    ): Float = brightness * intensity

    fun geometry(
        u: Float,
        v: Float,
        angle: Float,
        zoom: Float,
        pulseAmount: Float = 0f,
    ): Pair<Float, Float> {
        var cx = u - 0.5f
        var cy = v - 0.5f
        if (abs(angle) > 1e-4f) {
            val cs = cos(angle)
            val sn = sin(angle)
            val rx = cs * cx + sn * cy
            val ry = -sn * cx + cs * cy
            cx = rx
            cy = ry
        }
        if (abs(zoom - 1f) > 1e-4f) {
            val z = maxOf(zoom, MIN_ZOOM)
            cx /= z
            cy /= z
        }
        if (pulseAmount > 1e-4f) {
            val s = pulseScale(pulseAmount)
            cx /= s
            cy /= s
        }
        return (cx + 0.5f) to (cy + 0.5f)
    }

    fun luma(rgb: FloatArray): Float = rgb[0] * LUMA_R + rgb[1] * LUMA_G + rgb[2] * LUMA_B

    fun hueRotate(
        rgb: FloatArray,
        amount: Float,
    ): FloatArray {
        if (abs(amount) <= 1e-4f) return rgb.copyOf()
        val angle = amount * TAU_GLSL
        val cs = cos(angle)
        val sn = sin(angle)
        val g = luma(rgb)
        val kx = AXIS * rgb[2] - AXIS * rgb[1]
        val ky = AXIS * rgb[0] - AXIS * rgb[2]
        val kz = AXIS * rgb[1] - AXIS * rgb[0]
        return floatArrayOf(
            g + (rgb[0] - g) * cs + kx * sn,
            g + (rgb[1] - g) * cs + ky * sn,
            g + (rgb[2] - g) * cs + kz * sn,
        )
    }

    fun grade(
        rgb: FloatArray,
        hue: Float,
        saturation: Float,
        contrast: Float,
        gamma: Float,
        brightness: Float,
    ): FloatArray {
        var col = hueRotate(rgb, hue)
        if (abs(saturation - 1f) > 1e-4f) {
            val src = col
            val lum = luma(src)
            col = FloatArray(3) { lum + (src[it] - lum) * saturation }
        }
        if (abs(contrast - 1f) > 1e-4f) {
            val src = col
            col = FloatArray(3) { (src[it] - 0.5f) * contrast + 0.5f }
        }
        if (abs(gamma - 1f) > 1e-4f) {
            val src = col
            val inv = 1f / maxOf(gamma, MIN_GAMMA)
            col = FloatArray(3) { maxOf(src[it], 0f).pow(inv) }
        }
        val graded = col
        return FloatArray(3) { graded[it] * brightness }
    }

    const val TINT_EPSILON: Float = 0.001f

    const val TINT_CHROMA_KNEE: Float = 0.15f

    const val TINT_SAT_LIFT: Float = 0.35f

    fun paletteSpan(
        hueRange: Float,
        paletteRange: Float,
    ): Float = hueRange.coerceAtLeast(0f) * paletteRange.coerceIn(0f, 1f)

    fun paletteTintAmount(tint: Float): Float = tint.coerceIn(0f, 1f)

    fun paletteTint(
        rgb: FloatArray,
        base: Float,
        span: Float,
        amount: Float,
    ): FloatArray {
        if (amount <= TINT_EPSILON) return rgb.copyOf()
        val hsv = rgbToHsv(rgb)
        val chroma = smoothstep(0f, TINT_CHROMA_KNEE, hsv[1])
        val t = lerp(luma(rgb), hsv[0], chroma)
        val target = base + t * span
        val delta = fract(target - hsv[0] + 0.5f) - 0.5f
        return hsvToRgb(
            fract(hsv[0] + delta * amount),
            lerp(hsv[1], hsv[1] + (1f - hsv[1]) * TINT_SAT_LIFT, amount * (1f - chroma)),
            hsv[2],
        )
    }

    private fun fract(x: Float): Float = x - floor(x)

    private fun lerp(
        a: Float,
        b: Float,
        k: Float,
    ): Float = a + (b - a) * k

    private fun smoothstep(
        edge0: Float,
        edge1: Float,
        x: Float,
    ): Float {
        val t = ((x - edge0) / (edge1 - edge0)).coerceIn(0f, 1f)
        return t * t * (3f - 2f * t)
    }

    private fun rgbToHsv(rgb: FloatArray): FloatArray {
        val r = rgb[0]
        val g = rgb[1]
        val b = rgb[2]
        val mx = maxOf(r, maxOf(g, b))
        val mn = minOf(r, minOf(g, b))
        val d = mx - mn
        val h =
            when {
                d <= 0f -> 0f
                mx == r -> fract((g - b) / d / 6f)
                mx == g -> (2f + (b - r) / d) / 6f
                else -> (4f + (r - g) / d) / 6f
            }
        return floatArrayOf(h, if (mx <= 0f) 0f else d / mx, mx)
    }

    private fun hsvToRgb(
        h: Float,
        s: Float,
        v: Float,
    ): FloatArray {
        val k = floatArrayOf(1f, 2f / 3f, 1f / 3f)
        return FloatArray(3) {
            val p = abs(fract(h + k[it]) * 6f - 3f)
            v * lerp(1f, (p - 1f).coerceIn(0f, 1f), s)
        }
    }
}
