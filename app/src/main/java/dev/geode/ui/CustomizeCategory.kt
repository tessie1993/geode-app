package dev.geode.ui

import androidx.compose.runtime.Immutable
import dev.geode.engine.bridge.GeodeNative
import java.io.File

/**
 * One of Customize's categories, in panel order: its name and its tab title, both core/viz/CustomizeMap's.
 * Everything about a category is native (its sections and controls, which are free, which styles it shows
 * on, what its Randomize rolls and its Reset puts back); Kotlin holds what the map said and draws it.
 */
@Immutable
data class CustomizeCategory(
    val name: String,
    val title: String,
) {
    companion object {
        /** Every category, in panel order. */
        val entries: List<CustomizeCategory> get() = CustomizeMapTable.categories

        fun named(name: String?): CustomizeCategory? = entries.firstOrNull { it.name == name }

        // The categories the shell names: the knob tab, the one the reactions open on, and the tour's tabs.
        val QUICK_KNOBS: CustomizeCategory get() = of("QUICK_KNOBS")
        val LOOK: CustomizeCategory get() = of("LOOK")
        val SHAPE: CustomizeCategory get() = of("SHAPE")
        val MUSIC: CustomizeCategory get() = of("MUSIC")
        val ENGINE: CustomizeCategory get() = of("ENGINE")

        private fun of(name: String): CustomizeCategory = entries.first { it.name == name }
    }
}

/** One row of the native map, one collapsible group in its tab: a category's Basics (free) or a Pro section. */
internal data class CustomizeSection(
    val category: CustomizeCategory,
    /** Unique inside its category; the open ones are saved by [path]. */
    val id: String,
    val basics: Boolean,
    /** Customize map keys, or `@id` for a panel piece that is not one parameter. */
    val controls: List<String>,
    /** The words on the group's header. */
    val title: String,
) {
    val path: String get() = "${category.name}/$id"
}

/**
 * The native map's rows as the panel draws them (core/viz/CustomizeMap's `text()`). This splits lines into
 * rows and nothing more: no rule about a category, a section or a control is decided here.
 *
 * JVM unit tests cannot load libgeode; the build points [TEXT_FILE_PROPERTY] at `core/tests/customize.txt`,
 * which core/tests/customize_test proves byte-equal to what C++ emits.
 */
internal object CustomizeMapTable {
    /** core/viz/CustomizeMap.hpp's `kTextVersion`. */
    const val VERSION: Int = 2

    const val TEXT_FILE_PROPERTY: String = "geode.customize"

    private val text: String by lazy {
        System.getProperty(TEXT_FILE_PROPERTY)?.let { File(it).readText() } ?: GeodeNative.customizeText()
    }

    val categories: List<CustomizeCategory> by lazy { fields("category").map { CustomizeCategory(it[1], it[3]) } }

    val sections: List<CustomizeSection> by lazy {
        val byName = categories.associateBy { it.name }
        fields("section").mapNotNull { f ->
            val controls = f[5].split(',').filter(String::isNotEmpty)
            byName[f[1]]?.let { CustomizeSection(it, f[2], basics = f[3] == "BASICS", controls = controls, title = f[6]) }
        }
    }

    fun sectionsOf(category: CustomizeCategory): List<CustomizeSection> = sections.filter { it.category == category }

    /** Every control in a Basics section, as the engine says. */
    val basicControls: Set<String> by lazy { sections.filter { it.basics }.flatMapTo(mutableSetOf()) { it.controls } }

    /** What the panel offers on [styleId] (`CATEGORY` and `CATEGORY/id` lines). Native. */
    fun shownOn(styleId: String): Set<String> =
        GeodeNative
            .customizeShown(styleId)
            .lines()
            .filterTo(mutableSetOf()) { it.isNotBlank() }

    private fun fields(type: String): List<List<String>> {
        val rows = text.lineSequence().map { it.trimEnd('\r').split('|') }.toList()
        check(rows.firstOrNull()?.getOrNull(1)?.toIntOrNull() == VERSION) { "Customize map version ${rows.firstOrNull()}" }
        return rows.filter { it[0] == type }
    }
}

/** A word the Customize panel draws, by its id (core/viz/PanelText), asked of native once. */
internal fun panelText(id: String): String = panelWords.getOrPut(id) { GeodeNative.panelText(id) }

private val panelWords = java.util.concurrent.ConcurrentHashMap<String, String>()
