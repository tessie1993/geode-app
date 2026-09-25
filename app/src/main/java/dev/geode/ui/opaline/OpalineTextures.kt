package dev.geode.ui.opaline

import android.graphics.Bitmap
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.lerp
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.exp
import kotlin.math.pow
import kotlin.math.sin

/**
 * Textures baked once, off the main thread, from [OpalineNoise]. Nothing here is a shipped image:
 * the cloud tile and the environment are computed from the library's noise and the theme.
 */
internal object OpalineTextures {
    /**
     * A 256 px tile: red is the library's warped interior density (`opFbm(p + opFbm(p) * 1.6)`),
     * green an independent finer field for caustic breakup. Both tile seamlessly.
     */
    fun cloud(): Bitmap {
        val pixels = IntArray(CLOUD_SIZE * CLOUD_SIZE)
        val cells = CLOUD_CELLS.toFloat() / CLOUD_SIZE
        for (y in 0 until CLOUD_SIZE) {
            for (x in 0 until CLOUD_SIZE) {
                val px = x * cells
                val py = y * cells
                val warp = OpalineNoise.tileFbm(px, py, WARP_SLICE, CLOUD_CELLS)
                val density = OpalineNoise.tileFbm(px + warp * WARP_GAIN, py + warp * WARP_GAIN, 0f, CLOUD_CELLS)
                val fine = OpalineNoise.tileFbm(px * 2f, py * 2f, FINE_SLICE, CLOUD_CELLS * 2)
                val r = (density.coerceIn(0f, 1f) * 255f).toInt()
                val g = (fine.coerceIn(0f, 1f) * 255f).toInt()
                pixels[y * CLOUD_SIZE + x] = (0xFF shl 24) or (r shl 16) or (g shl 8) or r
            }
        }
        return Bitmap.createBitmap(pixels, CLOUD_SIZE, CLOUD_SIZE, Bitmap.Config.ARGB_8888)
    }

    private val backdrops = LinkedHashMap<Triple<OpalineThemeId, Int, Int>, Bitmap>()

    /**
     * The shared environment: an abstract deep liquid/mineral space (`PNG-TO-MOTION.md`), with
     * sculpted translucent folds lit by the same key light as every body, a faint caustic
     * network on the lower receiving plane and a large quiet middle for content.
     */
    @Synchronized
    fun backdrop(
        palette: OpalinePalette,
        width: Int,
        height: Int,
    ): Bitmap {
        val key = Triple(palette.theme, width, height)
        backdrops[key]?.let { return it }
        val bitmap = renderBackdrop(palette, width.coerceAtLeast(2), height.coerceAtLeast(2))
        backdrops[key] = bitmap
        while (backdrops.size > BACKDROP_CACHE) backdrops.remove(backdrops.keys.first())
        return bitmap
    }

    /**
     * A translucent liquid sheet crossing the space: its upper edge is a lit crest, its body
     * fades downward into the environment. Positions are fractions of the height; the middle
     * of the screen is left quiet for content.
     */
    private class Fold(
        val crest: Float,
        val tilt: Float,
        val amplitude: Float,
        val frequency: Float,
        val phase: Float,
        val depth: Float,
        val warp: Float,
        val opacity: Float,
        val shine: Float,
        val seed: Float,
    )

    private val FOLDS =
        listOf(
            Fold(0.17f, 0.16f, 0.05f, 0.55f, 0.3f, 0.16f, 0.05f, 0.22f, 0.5f, 3.3f),
            Fold(0.68f, -0.12f, 0.06f, 0.75f, 0.1f, 0.3f, 0.06f, 0.5f, 0.85f, 1.7f),
            Fold(0.82f, 0.08f, 0.045f, 1.05f, 0.62f, 0.26f, 0.05f, 0.62f, 1f, 9.1f),
        )

    private fun renderBackdrop(
        palette: OpalinePalette,
        width: Int,
        height: Int,
    ): Bitmap {
        val aspect = width.toFloat() / height
        val top = lerp(palette.environment, palette.blue, TOP_LIFT).rgb()
        val middle = palette.environment.rgb()
        val bottom = palette.environmentLow.rgb()
        val bloom = lerp(palette.gel, Color.White, 0.3f).rgb()
        val accent = palette.accent.rgb()
        val crestLight = lerp(palette.gel, Color.White, 0.55f).rgb()
        val foldLight = lerp(palette.blue, palette.gel, 0.35f).rgb()
        val foldDeep = lerp(palette.deep, palette.environmentLow, 0.45f).rgb()
        val curves = FOLDS.map { FloatArray(width) }
        val slopes = FOLDS.map { FloatArray(width) }
        FOLDS.forEachIndexed { index, fold ->
            val curve = curves[index]
            for (x in 0 until width) {
                val t = x / (width - 1f)
                curve[x] = fold.crest + fold.tilt * (t - 0.5f) +
                    fold.amplitude * sin(2f * PI.toFloat() * (fold.frequency * t + fold.phase)) +
                    fold.warp * (OpalineNoise.fbm(t * 1.4f, fold.seed, 0.5f) - 0.5f)
            }
            for (x in 0 until width) {
                val a = curve[(x - 1).coerceAtLeast(0)]
                val b = curve[(x + 1).coerceAtMost(width - 1)]
                slopes[index][x] = (b - a) * (width - 1) / 2f / aspect
            }
        }
        val pixels = IntArray(width * height)
        val col = FloatArray(3)
        for (y in 0 until height) {
            val v = y / (height - 1f)
            for (x in 0 until width) {
                val u = x / (width - 1f)
                val upper = (v / 0.5f).coerceIn(0f, 1f)
                val lower = ((v - 0.5f) / 0.5f).coerceIn(0f, 1f)
                for (c in 0..2) {
                    col[c] = if (v < 0.5f) top[c] + (middle[c] - top[c]) * upper else middle[c] + (bottom[c] - middle[c]) * lower
                }
                // The key light sits above and to the left; its bloom falls on the whole space.
                val gx = (u - 0.22f) * aspect
                val gy = v + 0.12f
                val glow = exp(-(gx * gx + gy * gy) / BLOOM_RADIUS)
                for (c in 0..2) col[c] += (bloom[c] - col[c]) * BLOOM_STRENGTH * glow

                FOLDS.forEachIndexed { index, fold ->
                    val s = (v - curves[index][x]) / fold.depth
                    if (s > -0.02f && s < 1f) {
                        val inside = s.coerceIn(0f, 1f)
                        // Rising toward the upper left faces the key light; the crest brightens there.
                        val facing = (0.5f - slopes[index][x] * 1.6f).coerceIn(0f, 1f)
                        val alpha = fold.opacity * smooth(-0.02f, 0.03f, s) * (1f - smooth(0.45f, 1f, s))
                        val crest = exp(-((s - 0.035f) / 0.03f).let { it * it }) * fold.shine * (0.45f + 0.55f * facing)
                        val caustic =
                            (1f - abs(2f * OpalineNoise.fbm(u * 7f * aspect, v * 11f, fold.seed) - 1f)).pow(9f) *
                                (1f - inside) * CAUSTIC_IN_FOLD
                        for (c in 0..2) {
                            val body = foldLight[c] + (foldDeep[c] - foldLight[c]) * inside.pow(0.6f)
                            col[c] += (body - col[c]) * alpha
                            col[c] += crestLight[c] * crest * 0.6f + accent[c] * caustic * alpha
                        }
                    }
                }

                if (v > FLOOR_START) {
                    val field = OpalineNoise.fbm(u * 5f * aspect, v * 9f, 2.3f)
                    val line = (1f - abs(2f * field - 1f)).pow(8f)
                    val fade = ((v - FLOOR_START) / (1f - FLOOR_START)).coerceIn(0f, 1f)
                    for (c in 0..2) col[c] += accent[c] * line * FLOOR_CAUSTIC * fade
                }
                val vx = u - 0.5f
                val vy = v - 0.42f
                val vignette = 1f - VIGNETTE * (vx * vx + vy * vy)
                val dither = (OpalineNoise.hash(x.toFloat(), y.toFloat(), 3f) - 0.5f) / 255f
                var argb = 0xFF shl 24
                for (c in 0..2) {
                    val value = (col[c] * vignette + dither).coerceIn(0f, 1f)
                    argb = argb or ((value * 255f + 0.5f).toInt() shl (16 - 8 * c))
                }
                pixels[y * width + x] = argb
            }
        }
        return Bitmap.createBitmap(pixels, width, height, Bitmap.Config.ARGB_8888)
    }

    private fun smooth(
        edge0: Float,
        edge1: Float,
        x: Float,
    ): Float {
        val t = ((x - edge0) / (edge1 - edge0)).coerceIn(0f, 1f)
        return t * t * (3f - 2f * t)
    }

    private fun Color.rgb(): FloatArray = floatArrayOf(red, green, blue)

    private const val CLOUD_SIZE = 256
    private const val CLOUD_CELLS = 4
    private const val WARP_SLICE = 3.1f
    private const val FINE_SLICE = 11.7f
    private const val WARP_GAIN = 1.6f
    private const val BACKDROP_CACHE = 2
    private const val TOP_LIFT = 0.14f
    private const val BLOOM_RADIUS = 0.3f
    private const val BLOOM_STRENGTH = 0.16f
    private const val CAUSTIC_IN_FOLD = 0.35f
    private const val FLOOR_START = 0.55f
    private const val FLOOR_CAUSTIC = 0.06f
    private const val VIGNETTE = 0.55f
}
