package dev.geode.render

/**
 * The parameter matrix as C++ owns it (`core/viz/ParamMatrix`, read over JNI with
 * `GeodeNative.paramMatrixCatalog`): every sound value the engine hears (the rows) and every visual
 * parameter a route can move (the columns), each in wire order. The Customize grid, the pickers and
 * the range sliders take names, labels, groups and the two ends of each sound from here.
 * [ReactSource] and [LfoTarget] are wire mirrors that `ParamMatrixCatalogDeviceTest` checks against it.
 */
data class ReactSourceInfo(
    val id: Int,
    val name: String,
    val label: String,
    val group: String,
    val isEvent: Boolean,
    /** What the low end of this sound means ("low note", "dark", "between kicks"). */
    val low: String = "",
    /** What the high end means ("high note", "bright", "on the kick"). */
    val high: String = "",
)

data class ReactTargetInfo(
    val id: Int,
    val name: String,
    val label: String,
    val group: String,
    val isLuminance: Boolean,
)

data class ParamMatrixCatalog(
    val sources: List<ReactSourceInfo>,
    val targets: List<ReactTargetInfo>,
) {
    fun source(s: ReactSource): ReactSourceInfo? = sources.getOrNull(s.ordinal)

    companion object {
        /**
         * Parses "S|NAME|Label|GROUP|0/1|low|high" and "T|NAME|Label|GROUP|0/1" lines. A malformed
         * line is skipped without shifting the ids of the rest, which count per kind.
         */
        fun parse(text: String): ParamMatrixCatalog {
            val sources = mutableListOf<ReactSourceInfo>()
            val targets = mutableListOf<ReactTargetInfo>()
            var s = 0
            var t = 0
            text.lineSequence().filter { it.isNotBlank() }.forEach { line ->
                val f = line.split('|')
                when (f.firstOrNull()) {
                    "S" -> {
                        if (f.size == SOURCE_FIELDS) sources += ReactSourceInfo(s, f[1], f[2], f[3], f[4] == "1", f[5], f[6])
                        s++
                    }
                    "T" -> {
                        if (f.size == TARGET_FIELDS) targets += ReactTargetInfo(t, f[1], f[2], f[3], f[4] == "1")
                        t++
                    }
                }
            }
            return ParamMatrixCatalog(sources, targets)
        }

        private const val SOURCE_FIELDS = 7
        private const val TARGET_FIELDS = 5
    }
}
