package dev.geode.render

/**
 * The reaction source catalogue as C++ owns it (`reactSourceInfo` in core/viz/Reactions.cpp, read over
 * JNI with `GeodeNative.reactSourceCatalog`): one entry per source in wire order. The Customize feature
 * builds its pickers, recipes and saved names from this; [ReactSource] is a mirror that
 * `ReactSourceCatalogTest` (androidTest) checks against it until the feature reads the catalogue alone.
 */
data class ReactSourceInfo(
    val id: Int,
    val name: String,
    val label: String,
    val group: String,
    val isEvent: Boolean,
)

object ReactSourceCatalog {
    /** Parses the "NAME|Label|GROUP|0/1" lines; a malformed line is skipped, keeping the ids of the rest. */
    fun parse(text: String): List<ReactSourceInfo> =
        text
            .lineSequence()
            .filter { it.isNotBlank() }
            .mapIndexedNotNull { id, line ->
                val f = line.split('|')
                if (f.size != FIELDS) null else ReactSourceInfo(id, f[0], f[1], f[2], f[3] == "1")
            }.toList()

    private const val FIELDS = 4
}
