package dev.geode.export

import android.content.Context
import android.graphics.Color
import android.net.Uri
import kotlin.math.pow
import kotlin.math.roundToInt

/**
 * A colour lookup table in the shape Media3's `SingleColorLut.createFromCube` wants: `cube[r][g][b]` is
 * the ARGB output for that input. Parsed from Adobe/Resolve `.cube` text (3D, or 1D expanded per channel).
 */
class CubeLut private constructor(
    val size: Int,
    val cube: Array<Array<IntArray>>,
) {
    companion object {
        const val GAMMA_SIZE = 33
        private const val MAX_SIZE = 65

        fun load(
            context: Context,
            uri: String,
        ): CubeLut? =
            runCatching {
                context.contentResolver.openInputStream(Uri.parse(uri))?.use { parse(it.readBytes().toString(Charsets.UTF_8)) }
            }.getOrNull()

        fun parse(text: String): CubeLut? {
            var size3d = 0
            var size1d = 0
            var domainMin = floatArrayOf(0f, 0f, 0f)
            var domainMax = floatArrayOf(1f, 1f, 1f)
            val rows = ArrayList<FloatArray>()
            for (raw in text.lineSequence()) {
                val line = raw.substringBefore('#').trim()
                if (line.isEmpty()) continue
                val parts = line.split(Regex("\\s+"))
                when (parts[0].uppercase()) {
                    "TITLE" -> Unit
                    "LUT_3D_SIZE" -> size3d = parts.getOrNull(1)?.toIntOrNull() ?: 0
                    "LUT_1D_SIZE" -> size1d = parts.getOrNull(1)?.toIntOrNull() ?: 0
                    "DOMAIN_MIN" -> domainMin = triple(parts) ?: domainMin
                    "DOMAIN_MAX" -> domainMax = triple(parts) ?: domainMax
                    else -> triple(parts, 0)?.let(rows::add)
                }
            }
            val span = FloatArray(3) { (domainMax[it] - domainMin[it]).takeIf { d -> d > 0f } ?: 1f }

            fun norm(
                v: Float,
                c: Int,
            ): Float = ((v - domainMin[c]) / span[c]).coerceIn(0f, 1f)
            return when {
                size3d in 2..MAX_SIZE && rows.size >= size3d * size3d * size3d -> {
                    val n = size3d
                    CubeLut(
                        n,
                        Array(n) { r ->
                            Array(n) { g ->
                                IntArray(n) { b ->
                                    val row = rows[r + g * n + b * n * n]
                                    argb(norm(row[0], 0), norm(row[1], 1), norm(row[2], 2))
                                }
                            }
                        },
                    )
                }
                size1d in 2..MAX_SIZE && rows.size >= size1d -> {
                    val n = size1d
                    CubeLut(
                        n,
                        Array(n) { r ->
                            Array(n) { g ->
                                IntArray(n) { b -> argb(norm(rows[r][0], 0), norm(rows[g][1], 1), norm(rows[b][2], 2)) }
                            }
                        },
                    )
                }
                else -> null
            }
        }

        /** Per-channel gamma as a cube: output = input ^ (1 / gamma) on each channel. */
        fun gamma(
            red: Float,
            green: Float,
            blue: Float,
            size: Int = GAMMA_SIZE,
        ): CubeLut {
            val last = (size - 1).toFloat()
            val r = FloatArray(size) { (it / last).pow(1f / red.coerceAtLeast(0.01f)) }
            val g = FloatArray(size) { (it / last).pow(1f / green.coerceAtLeast(0.01f)) }
            val b = FloatArray(size) { (it / last).pow(1f / blue.coerceAtLeast(0.01f)) }
            return CubeLut(size, Array(size) { ri -> Array(size) { gi -> IntArray(size) { bi -> argb(r[ri], g[gi], b[bi]) } } })
        }

        private fun triple(
            parts: List<String>,
            from: Int = 1,
        ): FloatArray? {
            val r = parts.getOrNull(from)?.toFloatOrNull() ?: return null
            val g = parts.getOrNull(from + 1)?.toFloatOrNull() ?: return null
            val b = parts.getOrNull(from + 2)?.toFloatOrNull() ?: return null
            return floatArrayOf(r, g, b)
        }

        private fun argb(
            r: Float,
            g: Float,
            b: Float,
        ): Int =
            Color.argb(
                255,
                (r * 255f).roundToInt().coerceIn(0, 255),
                (g * 255f).roundToInt().coerceIn(0, 255),
                (b * 255f).roundToInt().coerceIn(0, 255),
            )
    }
}
