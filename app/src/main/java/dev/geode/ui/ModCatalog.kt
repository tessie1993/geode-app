package dev.geode.ui

import dev.geode.engine.bridge.GeodeNative
import dev.geode.util.bestEffort

/** One row native publishes for a picker: its wire ordinal, the name a saved text carries and the words shown. */
internal data class ModWord(
    val ordinal: Int,
    val name: String,
    val label: String,
    /** The row's further fields: a reaction source's group key and event mark, a recipe's sentence. */
    val more: List<String> = emptyList(),
)

/**
 * What the modulation editors show, read once from native (core/viz/ModCatalog, `geode_mod_catalog_text`):
 * every wave shape, source, direction, curve, band, mode, target and recipe, the slider ranges and the slot
 * counts. The editors keep no list, range or default of their own; this only splits the rows.
 */
internal object ModCatalog {
    private const val TAG = "ModCatalog"
    private const val WORD_FIELDS = 4
    private const val MIN = 3
    private const val MAX = 4

    private val rows: Map<String, List<List<String>>> by lazy {
        var text = ""
        bestEffort(TAG, "modCatalogText") { text = GeodeNative.modCatalogText() }
        text.lines().map { it.split('|') }.groupBy { it[0] }
    }

    private val words: Map<String, List<ModWord>> by lazy {
        rows.mapValues { (_, lines) ->
            lines.filter { it.size >= WORD_FIELDS }.map { ModWord(it[1].toIntOrNull() ?: 0, it[2], it[3], it.drop(WORD_FIELDS)) }
        }
    }

    /** Every row of one [kind] (`target`, `source`, `wave`, `react`, `recipe`, ...), in native's order. */
    fun words(kind: String): List<ModWord> = words[kind].orEmpty()

    /** The words a picker shows for [ordinal] of [kind]; empty for one native does not list. */
    fun label(
        kind: String,
        ordinal: Int,
    ): String = words(kind).firstOrNull { it.ordinal == ordinal }?.label.orEmpty()

    /** A slot count: `waves`, `envelopes` or `routes`. */
    fun count(name: String): Int = row("count", name)?.getOrNull(2)?.toIntOrNull() ?: 0

    /** A slider's reach, `lfo.depth` to `music.strength`. */
    fun range(name: String): ClosedFloatingPointRange<Float> {
        val r = row("range", name)
        return (r?.getOrNull(MIN)?.toFloatOrNull() ?: 0f)..(r?.getOrNull(MAX)?.toFloatOrNull() ?: 1f)
    }

    private fun row(
        kind: String,
        name: String,
    ): List<String>? = rows[kind]?.firstOrNull { it.getOrNull(1) == name }
}
