package dev.geode.engine.audio

import kotlin.math.cos

enum class WindowShape {
    RECTANGULAR,

    HANN,
}

class WindowTable(
    val size: Int,
    val shape: WindowShape = WindowShape.HANN,
) {
    init {
        require(size > 0) { "size must be positive, was $size" }
    }

    private val table =
        FloatArray(size) { i ->
            when (shape) {
                WindowShape.RECTANGULAR -> 1f
                WindowShape.HANN -> (0.5 - 0.5 * cos(2.0 * Math.PI * i / size)).toFloat()
            }
        }

    fun coefficient(index: Int): Float = table[index]

    fun applyInto(
        source: FloatArray,
        sourceOffset: Int,
        out: FloatArray,
    ) {
        require(out.size >= size) { "out holds ${out.size}, window needs $size" }
        for (i in 0 until size) {
            val at = sourceOffset + i
            out[i] = if (at in source.indices) source[at] * table[i] else 0f
        }
    }
}
