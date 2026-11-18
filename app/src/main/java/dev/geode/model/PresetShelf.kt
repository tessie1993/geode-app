package dev.geode.model

import dev.geode.engine.bridge.ShellNative

/**
 * One preset as the Presets list sees it. The list never needs the look itself (params, shader
 * text, a MilkDrop file), only what decides which rows show, in which order and with which marks;
 * those rules are native's ([PresetFiltering]). The page builds these from `data/Preset`.
 *
 * [forThisStyle] is worked out by the page, since the style families (every Cymatics variant
 * sharing the Cymatics presets) live in the catalogue. [onStage] says the stage shows this
 * preset's look right now: same style, same params.
 */
data class PresetEntry(
    val name: String,
    val sceneId: String,
    val builtIn: Boolean,
    val folder: String = "",
    val forThisStyle: Boolean = false,
    val onStage: Boolean = false,
)

/**
 * The Presets list's filter chips. One row of chips replaced a long list with a header for each
 * folder and a second list of built-ins: the shelves the user reaches for first, then each of
 * their folders.
 */
sealed interface PresetFilter {
    data object All : PresetFilter

    data object ThisStyle : PresetFilter

    data object Mine : PresetFilter

    data object BuiltIn : PresetFilter

    data class Folder(
        val name: String,
    ) : PresetFilter
}

/**
 * The Presets list's rules, native's (core/shell/PresetList through [ShellNative]): which chips show, which
 * rows a chip leaves and in what order, and the one row that gets the check. Kotlin only hands the rows over
 * and maps the answers back.
 */
object PresetFiltering {
    /** The chips in order: the four shelves, then the user's folders as the store lists them (blank ones dropped, each once). */
    fun filters(folders: List<String>): List<PresetFilter> =
        ShellNative.presetFilters(folders.toTypedArray()).asList().chunked(2).map { (kind, folder) ->
            when (kind) {
                ShellNative.PRESET_FILTER_ALL -> PresetFilter.All
                ShellNative.PRESET_FILTER_THIS_STYLE -> PresetFilter.ThisStyle
                ShellNative.PRESET_FILTER_MINE -> PresetFilter.Mine
                ShellNative.PRESET_FILTER_BUILT_IN -> PresetFilter.BuiltIn
                else -> PresetFilter.Folder(folders[folder])
            }
        }

    /**
     * The rows [filter] leaves, in list order: the user's own first (what they made is what they
     * came for), then the built-ins, those made for the style on stage ahead of the rest. A name
     * shows once: the store can hold a user preset that shadows nothing, but a list read twice
     * (the session merges the built-ins and the files) must not draw two rows for it.
     */
    fun apply(
        entries: List<PresetEntry>,
        filter: PresetFilter,
    ): List<PresetEntry> {
        val kind =
            when (filter) {
                PresetFilter.All -> ShellNative.PRESET_FILTER_ALL
                PresetFilter.ThisStyle -> ShellNative.PRESET_FILTER_THIS_STYLE
                PresetFilter.Mine -> ShellNative.PRESET_FILTER_MINE
                PresetFilter.BuiltIn -> ShellNative.PRESET_FILTER_BUILT_IN
                is PresetFilter.Folder -> ShellNative.PRESET_FILTER_FOLDER
            }
        val rows =
            ShellNative.presetApply(
                names(entries),
                entries.map { it.folder }.toTypedArray(),
                flags(entries),
                kind,
                (filter as? PresetFilter.Folder)?.name,
            )
        return rows.map { entries[it] }
    }

    /**
     * The one row that gets the check. The preset the user last tapped wins while the stage
     * still shows its look; otherwise the first preset whose look the stage shows, so a look
     * applied from somewhere else (a shared link, auto-visuals) is marked too. Null once the
     * user has changed anything, since no preset is on stage then.
     */
    fun applied(
        entries: List<PresetEntry>,
        lastTapped: String?,
    ): String? = entries.getOrNull(ShellNative.presetApplied(names(entries), flags(entries), lastTapped))?.name

    private fun names(entries: List<PresetEntry>): Array<String> = entries.map { it.name }.toTypedArray()

    private fun flags(entries: List<PresetEntry>): IntArray =
        IntArray(entries.size) { i ->
            val e = entries[i]
            (if (e.builtIn) ShellNative.PRESET_BUILT_IN else 0) or
                (if (e.forThisStyle) ShellNative.PRESET_FOR_THIS_STYLE else 0) or
                (if (e.onStage) ShellNative.PRESET_ON_STAGE else 0)
        }
}
