package dev.geode.ui

import dev.geode.engine.bridge.ResponderNative
import dev.geode.util.bestEffort

/** A native word with the sentence under it: a strength curve or a drive mode. */
internal data class ResponseWord(
    val name: String,
    val sentence: String,
)

/** One event row as the page lists it: its Moment ordinal, its name and the group it sits in. */
internal data class EventRowInfo(
    val row: Int,
    val label: String,
    val category: Int,
    val needsBeat: Boolean,
)

/** A tile of the "+ Add response" sheet. */
internal data class ResponseTile(
    val ordinal: Int,
    val family: Int,
    val name: String,
    val line: String,
)

/** One control of a response kind: [field] is the `RESP_*` it edits, [unit] one of the `UNIT_*` below. */
internal data class ResponseControl(
    val field: Int,
    val label: String,
    val unit: Int,
    val range: ClosedFloatingPointRange<Float>,
    val step: Float,
    val choices: List<String>,
)

/**
 * What the Song events page draws and keeps no copy of, read once from native (core/viz/modulation/ResponseCatalog,
 * `geode_resp_catalog_text`): the families and tiles, each kind's controls, the envelope presets, the strength
 * curves, the drive modes and the rows' names. This only splits the lines.
 */
internal class ResponseCatalogue(
    val families: List<String>,
    val presets: List<String>,
    val curves: List<ResponseWord>,
    val drives: List<ResponseWord>,
    val categories: List<String>,
    val rows: List<EventRowInfo>,
    val tiles: List<ResponseTile>,
    val kindControls: Map<Int, List<ResponseControl>>,
    val common: List<ResponseControl>,
) {
    /** The controls of response [kind] that are its own; an unknown kind has none. */
    fun controlsOf(kind: Int): List<ResponseControl> = kindControls[kind].orEmpty()

    companion object {
        // Mirrors of core/viz/modulation/ResponseCatalog's Unit.
        const val UNIT_AMOUNT: Int = 0
        const val UNIT_PERCENT: Int = 1
        const val UNIT_COUNT: Int = 2
        const val UNIT_COUNT_OFF: Int = 3
        const val UNIT_SECONDS: Int = 4
        const val UNIT_CHOICE: Int = 5

        private const val CONTROL_CHOICES = 8
        private const val COMMON_FIELDS = 7

        /** The catalogue native holds. JVM unit tests cannot load libgeode and parse a text of their own with [parse]. */
        val live: ResponseCatalogue by lazy {
            var text = ""
            bestEffort("ResponseCatalogue", "responderCatalogText") { text = ResponderNative.responderCatalogText() }
            parse(text)
        }

        fun parse(text: String): ResponseCatalogue {
            val lines =
                text
                    .lines()
                    .filter { it.isNotEmpty() }
                    .map { it.split('|') }
                    .groupBy { it[0] }

            fun of(kind: String) = lines[kind].orEmpty()

            fun control(
                field: Int,
                label: String,
                unit: String,
                min: String,
                max: String,
                step: String,
                choices: List<String>,
            ) = ResponseControl(
                field,
                label,
                unit.toIntOrNull() ?: UNIT_PERCENT,
                (min.toFloatOrNull() ?: 0f)..(max.toFloatOrNull() ?: 1f),
                step.toFloatOrNull() ?: 0f,
                choices,
            )
            return ResponseCatalogue(
                families = of("family").map { it[2] },
                presets = of("preset").map { it[2] },
                curves = of("curve").map { ResponseWord(it[2], it.getOrElse(3) { "" }) },
                drives = of("drive").map { ResponseWord(it[2], it.getOrElse(3) { "" }) },
                categories = of("category").map { it[2] },
                rows = of("row").map { EventRowInfo(it[1].toInt(), it[3], it[4].toInt(), it[5] == "1") },
                tiles = of("entry").map { ResponseTile(it[1].toInt(), it[2].toInt(), it[4], it[5]) },
                kindControls =
                    of("control")
                        .groupBy({ it[1].toInt() }) {
                            control(it[2].toInt(), it[3], it[4], it[5], it[6], it[7], it.drop(CONTROL_CHOICES))
                        },
                common =
                    of("common")
                        .filter { it.size >= COMMON_FIELDS }
                        .map { control(it[1].toInt(), it[2], it[3], it[4], it[5], it[6], emptyList()) },
            )
        }
    }
}
