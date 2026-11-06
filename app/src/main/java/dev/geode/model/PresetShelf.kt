package dev.geode.model

/**
 * One preset as the Presets grid sees it. The grid never needs the look itself (params, shader
 * text, a MilkDrop file), only what decides which cards show, in which order and with which marks,
 * so the rules below stay pure and are tested off-device. The page builds these from `data/Preset`.
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
 * The Presets grid's filter chips: the shelves the user reaches for first, then each of their
 * folders. There is no Built-in shelf: the built-in looks are styles, listed on the Styles page,
 * so the grid holds only what the user saved and every style lives in one place.
 */
sealed interface PresetFilter {
    data object All : PresetFilter

    data object ThisStyle : PresetFilter

    data class Folder(
        val name: String,
    ) : PresetFilter
}

object PresetFiltering {
    /** The chips in order: the two shelves, then the user's folders as the store lists them. */
    fun filters(folders: List<String>): List<PresetFilter> =
        listOf(PresetFilter.All, PresetFilter.ThisStyle) +
            folders.filter { it.isNotBlank() }.distinct().map { PresetFilter.Folder(it) }

    /**
     * The cards [filter] leaves, in the store's order: the user's own presets only (the built-ins
     * are styles, on the Styles page). A name shows once: a list read twice (the session merges
     * the built-ins and the files) must not draw two cards for it.
     */
    fun apply(
        entries: List<PresetEntry>,
        filter: PresetFilter,
    ): List<PresetEntry> {
        val mine = entries.filterNot { it.builtIn }
        val shown =
            when (filter) {
                PresetFilter.All -> mine
                PresetFilter.ThisStyle -> mine.filter { it.forThisStyle }
                is PresetFilter.Folder -> mine.filter { it.folder == filter.name }
            }
        return shown.distinctBy { it.name }
    }

    /**
     * The one card that gets the check. The preset the user last tapped wins while the stage
     * still shows its look; otherwise the first preset whose look the stage shows, so a look
     * applied from somewhere else (a shared link, auto-visuals) is marked too. Null once the
     * user has changed anything, since no preset is on stage then.
     */
    fun applied(
        entries: List<PresetEntry>,
        lastTapped: String?,
    ): String? {
        val onStage = entries.filter { it.onStage }
        return onStage.firstOrNull { it.name == lastTapped }?.name ?: onStage.firstOrNull()?.name
    }

    /** A name as the store keeps it: the share link's separator cannot appear inside one. */
    fun cleanName(raw: String): String = raw.replace(" · ", " - ").trim()

    /**
     * Why [raw] cannot be the new name of [current], or null when it can. [taken] is every name
     * in use, built-ins included: a user preset under a built-in's name would hide behind it.
     * Case is ignored, because on some file systems two such names are the same file.
     */
    fun renameError(
        raw: String,
        current: String,
        taken: Collection<String>,
    ): String? {
        val name = cleanName(raw)
        return when {
            name.isEmpty() -> "Give it a name."
            name == current -> null
            taken.any { it != current && it.equals(name, ignoreCase = true) } -> "There is already a preset called \"$name\"."
            else -> null
        }
    }

    /** [base], or "[base] 2", "[base] 3" and so on: the first that [taken] does not hold. */
    fun uniqueName(
        base: String,
        taken: Collection<String>,
    ): String {
        val lower = taken.map { it.lowercase() }.toSet()
        if (base.lowercase() !in lower) return base
        var n = 2
        while ("$base $n".lowercase() in lower) n++
        return "$base $n"
    }
}
