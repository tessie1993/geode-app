package dev.geode.ui.opaline

import kotlin.math.cos
import kotlin.math.floor
import kotlin.math.sin

/**
 * CPU port of `NOISE_3D` in `src/shaders/opaline.js` (`opHash`, `opNoise`, `opFbm`, `opFlow`),
 * in 32-bit floats like GLSL `highp`.
 *
 * [tileFbm] is the one departure: `opFbm` rotates and scales each octave by 2.03, which cannot
 * tile, so the texture variant doubles each octave exactly without rotation (keeping the
 * library's per-octave offset, gain and hash) and wraps the lattice at [period] cells.
 */
internal object OpalineNoise {
    fun hash(
        x: Float,
        y: Float,
        z: Float,
    ): Float {
        var px = fract(x * HASH_SCALE)
        var py = fract(y * HASH_SCALE)
        var pz = fract(z * HASH_SCALE)
        val d = px * (py + HASH_OFFSET) + py * (pz + HASH_OFFSET) + pz * (px + HASH_OFFSET)
        px += d
        py += d
        pz += d
        return fract((px + py) * pz)
    }

    /** `opNoise`: trilinear value noise with a smoothstep fade. [period] > 0 wraps x and y. */
    fun noise(
        x: Float,
        y: Float,
        z: Float,
        period: Int = 0,
    ): Float {
        val ix = floor(x)
        val iy = floor(y)
        val iz = floor(z)
        val fx = fade(x - ix)
        val fy = fade(y - iy)
        val fz = fade(z - iz)
        val x0 = wrap(ix, period)
        val x1 = wrap(ix + 1f, period)
        val y0 = wrap(iy, period)
        val y1 = wrap(iy + 1f, period)
        val z1 = iz + 1f
        val a = mix(mix(hash(x0, y0, iz), hash(x1, y0, iz), fx), mix(hash(x0, y1, iz), hash(x1, y1, iz), fx), fy)
        val b = mix(mix(hash(x0, y0, z1), hash(x1, y0, z1), fx), mix(hash(x0, y1, z1), hash(x1, y1, z1), fx), fy)
        return mix(a, b, fz)
    }

    /** `opFbm`, exactly: five rotated octaves at x2.03 with gain 0.5. */
    fun fbm(
        x: Float,
        y: Float,
        z: Float,
    ): Float {
        var f = 0f
        var a = 0.5f
        var px = x
        var py = y
        var pz = z
        repeat(OCTAVES) {
            f += a * noise(px, py, pz)
            val rx = -0.8f * py - 0.6f * pz
            val ry = 0.8f * px + 0.36f * py - 0.48f * pz
            val rz = 0.6f * px - 0.48f * py + 0.64f * pz
            px = rx * LACUNARITY + OCTAVE_OFFSET_X
            py = ry * LACUNARITY + OCTAVE_OFFSET_Y
            pz = rz * LACUNARITY + OCTAVE_OFFSET_Z
            a *= 0.5f
        }
        return f
    }

    /** Tileable fbm over a [period]-cell square; see the class note. */
    fun tileFbm(
        x: Float,
        y: Float,
        z: Float,
        period: Int,
    ): Float {
        var f = 0f
        var a = 0.5f
        var px = x
        var py = y
        var pz = z
        var cells = period
        repeat(OCTAVES) {
            f += a * noise(px, py, pz, cells)
            px = px * 2f + OCTAVE_OFFSET_X
            py = py * 2f + OCTAVE_OFFSET_Y
            pz = pz * 2f + OCTAVE_OFFSET_Z
            cells *= 2
            a *= 0.5f
        }
        return f
    }

    /** `opFlow`: the analytic cyclic field that advects the library's visual density. */
    fun flow(
        x: Float,
        y: Float,
        z: Float,
        t: Float,
        out: FloatArray,
    ) {
        out[0] = sin(y * 1.13f + t * 0.17f) + cos(z * 0.97f - t * 0.13f)
        out[1] = sin(z * 1.07f + t * 0.11f) + cos(x * 1.21f + t * 0.09f)
        out[2] = sin(x * 0.91f - t * 0.15f) + cos(y * 1.03f + t * 0.12f)
    }

    private fun fract(v: Float): Float = v - floor(v)

    private fun fade(t: Float): Float = t * t * (3f - 2f * t)

    private fun mix(
        a: Float,
        b: Float,
        t: Float,
    ): Float = a + (b - a) * t

    private fun wrap(
        i: Float,
        period: Int,
    ): Float {
        if (period <= 0) return i
        val m = i % period
        return if (m < 0f) m + period else m
    }

    private const val HASH_SCALE = 0.1031f
    private const val HASH_OFFSET = 33.33f
    private const val OCTAVES = 5
    private const val LACUNARITY = 2.03f
    private const val OCTAVE_OFFSET_X = 4.7f
    private const val OCTAVE_OFFSET_Y = 1.3f
    private const val OCTAVE_OFFSET_Z = 7.9f
}
