package dev.geode.render

/**
 * The parameter matrix as C++ owns it (`core/viz/ParamMatrix`, read over JNI with
 * `GeodeNative.paramMatrixCatalog`): every sound value the engine hears (the rows) and every visual
 * parameter a route can move (the columns), each in wire order, plus the routes the engine builds for
 * the shell: what a tap on a grid cell makes for each sound, the depths successive taps step through,
 * and the reaction presets. The Customize grid, the pickers and the range sliders take everything from
 * here and build no route of their own (ADR 0003). [ReactSource] and [LfoTarget] are wire mirrors that
 * `ParamMatrixCatalogDeviceTest` checks against it.
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
    /** What a tap on this sound's grid cell makes (native `matrix::smart`): the mode and its timing. */
    val smartMode: ReactMode = ReactMode.FOLLOW,
    val attackSeconds: Float = 0.08f,
    val releaseSeconds: Float = 0.35f,
)

data class ReactTargetInfo(
    val id: Int,
    val name: String,
    val label: String,
    val group: String,
    val isLuminance: Boolean,
    /** The range native clamps the parameter to, and how far a route's 1.0 moves it. */
    val min: Float = 0f,
    val max: Float = 1f,
    val scale: Float = 1f,
    /** For a motion the music drives (rotation, speed, fly ...): the sound that drives it by default. */
    val defaultDrive: String? = null,
)

/** A whole reaction set named by feel (native `matrix::presets`). */
data class ReactionPresetInfo(
    val id: String,
    val title: String,
    val description: String,
    val routes: List<ReactionRoute>,
)

data class ParamMatrixCatalog(
    val sources: List<ReactSourceInfo>,
    val targets: List<ReactTargetInfo>,
    /** The depths successive taps on one grid cell step through, then off (native `kCellSteps`). */
    val cellSteps: List<Float> = emptyList(),
    val presets: List<ReactionPresetInfo> = emptyList(),
) {
    fun source(s: ReactSource): ReactSourceInfo? = sources.getOrNull(s.ordinal)

    fun target(t: LfoTarget): ReactTargetInfo? = targets.getOrNull(t.ordinal)

    /**
     * The route native builds for [source] on [target] at [amount] (`matrix::cell`): the source's own
     * mode and timing, or Drive when the lane makes the music the motion itself.
     */
    fun cell(
        source: ReactSource,
        target: LfoTarget,
        amount: Float,
        drive: Boolean = false,
    ): ReactionRoute {
        val info = source(source)
        return ReactionRoute(
            enabled = true,
            source = source,
            target = target,
            mode = if (drive) ReactMode.DRIVE else info?.smartMode ?: ReactMode.FOLLOW,
            amount = amount.coerceIn(ReactionRoute.MIN_AMOUNT, ReactionRoute.MAX_AMOUNT),
            curve = ModCurve.LINEAR,
            attackSeconds = info?.attackSeconds ?: DEFAULT_ATTACK,
            releaseSeconds = info?.releaseSeconds ?: DEFAULT_RELEASE,
        )
    }

    companion object {
        /**
         * Parses the native catalogue (`matrix::catalogueText`): S, T, L, P and R lines. A malformed S or
         * T line is skipped without shifting the ids of the rest, which count per kind; an R line belongs
         * to the P line before it, and one naming an ordinal this build does not know is dropped.
         */
        fun parse(text: String): ParamMatrixCatalog {
            val sources = mutableListOf<ReactSourceInfo>()
            val targets = mutableListOf<ReactTargetInfo>()
            var steps = emptyList<Float>()
            val presets = mutableListOf<ReactionPresetInfo>()
            var s = 0
            var t = 0
            text.lineSequence().filter { it.isNotBlank() }.forEach { line ->
                val f = line.split('|')
                when (f.firstOrNull()) {
                    "S" -> {
                        if (f.size == SOURCE_FIELDS) sources += sourceOf(s, f)
                        s++
                    }
                    "T" -> {
                        if (f.size == TARGET_FIELDS) targets += targetOf(t, f)
                        t++
                    }
                    "L" -> steps = f.drop(1).mapNotNull { it.toFloatOrNull() }
                    "P" -> if (f.size == PRESET_FIELDS) presets += ReactionPresetInfo(f[1], f[2], f[3], emptyList())
                    "R" -> {
                        val route = routeOf(f)
                        val last = presets.lastOrNull()
                        if (route != null && last != null) presets[presets.lastIndex] = last.copy(routes = last.routes + route)
                    }
                }
            }
            return ParamMatrixCatalog(sources, targets, steps, presets)
        }

        private fun sourceOf(
            id: Int,
            f: List<String>,
        ) = ReactSourceInfo(
            id = id,
            name = f[1],
            label = f[2],
            group = f[3],
            isEvent = f[4] == "1",
            low = f[5],
            high = f[6],
            smartMode = ReactMode.entries.firstOrNull { it.name == f[7] } ?: ReactMode.FOLLOW,
            attackSeconds = f[8].toFloatOrNull() ?: DEFAULT_ATTACK,
            releaseSeconds = f[9].toFloatOrNull() ?: DEFAULT_RELEASE,
        )

        private fun targetOf(
            id: Int,
            f: List<String>,
        ) = ReactTargetInfo(
            id = id,
            name = f[1],
            label = f[2],
            group = f[3],
            isLuminance = f[4] == "1",
            min = f[5].toFloatOrNull() ?: 0f,
            max = f[6].toFloatOrNull() ?: 1f,
            scale = f[7].toFloatOrNull() ?: 1f,
            defaultDrive = f[8].takeIf { it != "-" },
        )

        private fun routeOf(f: List<String>): ReactionRoute? {
            if (f.size != ROUTE_FIELDS) return null
            val source = f[1].toIntOrNull()?.let { ReactSource.entries.getOrNull(it) } ?: return null
            val target = f[2].toIntOrNull()?.let { LfoTarget.entries.getOrNull(it) } ?: return null
            val mode = f[3].toIntOrNull()?.let { ReactMode.entries.getOrNull(it) } ?: return null
            val curve = f[5].toIntOrNull()?.let { ModCurve.entries.getOrNull(it) } ?: return null
            return ReactionRoute(
                enabled = true,
                source = source,
                target = target,
                mode = mode,
                amount = f[4].toFloatOrNull() ?: return null,
                curve = curve,
                attackSeconds = f[6].toFloatOrNull() ?: return null,
                releaseSeconds = f[7].toFloatOrNull() ?: return null,
            )
        }

        private const val SOURCE_FIELDS = 10
        private const val TARGET_FIELDS = 9
        private const val PRESET_FIELDS = 4
        private const val ROUTE_FIELDS = 8
        private const val DEFAULT_ATTACK = 0.08f
        private const val DEFAULT_RELEASE = 0.35f
    }
}
