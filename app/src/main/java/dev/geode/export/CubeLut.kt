package dev.geode.export

import android.content.Context
import android.net.Uri
import dev.geode.data.ReadCap
import dev.geode.engine.bridge.GeodeNative

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

        /** The `.cube` file at [uri], parsed natively (`core/util/CubeLut`); null when it is not a usable cube. */
        fun load(
            context: Context,
            uri: String,
        ): CubeLut? =
            runCatching {
                context.contentResolver.openInputStream(Uri.parse(uri))?.use { stream ->
                    GeodeNative.cubeLutParse(ReadCap.CUBE.read(stream))?.let(::fromFlat)
                }
            }.getOrNull()

        /** Per-channel gamma as a cube: output = input ^ (1 / gamma) on each channel. */
        fun gamma(
            red: Float,
            green: Float,
            blue: Float,
            size: Int = GAMMA_SIZE,
        ): CubeLut = checkNotNull(fromFlat(GeodeNative.cubeLutGamma(red, green, blue, size)))

        // Native hands the cube over flat, (r * n + g) * n + b; Media3 wants it nested [r][g][b].
        private fun fromFlat(argb: IntArray): CubeLut? {
            val n = cubeRoot(argb.size)
            if (n < 2) return null
            return CubeLut(n, Array(n) { r -> Array(n) { g -> IntArray(n) { b -> argb[(r * n + g) * n + b] } } })
        }

        private fun cubeRoot(entries: Int): Int {
            var n = 0
            while ((n + 1) * (n + 1) * (n + 1) <= entries) n++
            return if (n * n * n == entries) n else 0
        }
    }
}
