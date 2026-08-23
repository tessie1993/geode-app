package dev.geode.render.scene

object PcmRow {
    fun fill(
        dst: FloatArray,
        source: FloatArray,
        count: Int,
    ) {
        val n = count.coerceIn(0, source.size)
        if (n == 0) {
            dst.fill(0f)
            return
        }
        for (i in dst.indices) {
            val from = i * n / dst.size
            val to = ((i + 1) * n / dst.size).coerceAtLeast(from + 1).coerceAtMost(n)
            var extreme = 0f
            for (j in from until to) {
                val v = source[j]
                val safe = if (v.isFinite()) v else 0f
                if (kotlin.math.abs(safe) > kotlin.math.abs(extreme)) extreme = safe
            }
            dst[i] = extreme
        }
    }
}
