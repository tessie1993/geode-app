package dev.geode.engine.bridge

// The JNI declarations of geode_shell_jni.cpp (CLAUDE.md).
object ShellNative {
    init {
        GeodeNative.ensureLoaded()
    }

    // The shell's rules (core/shell through core/api/geode_shell_api.h).

    /** The frame rates an export offers, ascending (`geode_export_fps_options`). */
    external fun exportFpsOptions(): IntArray

    /** A stored frame rate kept when it is offered, else [fallback] (`geode_export_fps_read`). */
    external fun exportFpsRead(
        stored: Int,
        fallback: Int,
    ): Int

    /**
     * The media notification's extra buttons (core/shell/Behaviour). A button is its ordinal (shuffle, repeat,
     * favourite), a set of them the mask of `1 shl ordinal`. The [chosen] buttons in the order the session
     * offers them (`geode_notification_buttons_ordered`).
     */
    external fun notificationButtonsOrdered(chosen: Int): IntArray

    /** [chosen] with [button] switched; switching one on past the room drops the last other one (`geode_notification_buttons_toggled`). */
    external fun notificationButtonsToggled(
        chosen: Int,
        button: Int,
        on: Boolean,
    ): Int

    /** The last screen to keep, by ordinal: "last used" reads as the player (`geode_behaviour_last_screen`). */
    external fun behaviourLastScreen(last: Int): Int

    /** The screen to open on, by ordinal: [start], or [last] when [start] is "last used" (`geode_behaviour_opens_on`). */
    external fun behaviourOpensOn(
        start: Int,
        last: Int,
    ): Int

    /**
     * The Styles page's own shelf (core/shell/StyleShelf): favourites in the order they were starred, recents
     * newest first. [favourites] with [id] starred if it was not, unstarred if it was (`geode_style_shelf_toggle_favourite`).
     */
    external fun styleShelfToggleFavourite(
        favourites: Array<String>,
        id: String,
    ): Array<String>

    /** [recents] with [id] in front, no repeats, at most the recent limit (`geode_style_shelf_push_recent`). */
    external fun styleShelfPushRecent(
        recents: Array<String>,
        id: String,
    ): Array<String>

    /** [recents] without [id], its first place only (`geode_style_shelf_remove_recent`). */
    external fun styleShelfRemoveRecent(
        recents: Array<String>,
        id: String,
    ): Array<String>

    /**
     * Your styles: [onScreen] (null for none), then the favourites in [order]'s order, then the recents, each of
     * [order]'s ids once (`geode_style_shelf_yours`).
     */
    external fun styleShelfYours(
        favourites: Array<String>,
        recents: Array<String>,
        order: Array<String>,
        onScreen: String?,
    ): Array<String>

    /**
     * The Presets list (core/shell/PresetList). Its filter chips in order, the four shelves and then [folders]
     * (blank ones dropped, each once), as (kind, folder index) pairs: the kind a [PRESET_FILTER_ALL] constant,
     * the folder index into [folders], -1 for a shelf (`geode_preset_filters`).
     */
    external fun presetFilters(folders: Array<String>): IntArray

    /**
     * The rows a filter ([filterKind], and [filterFolder] for a folder chip) leaves, as indices into the rows
     * ([names], [folders] and [flags], the [PRESET_BUILT_IN] marks), in list order (`geode_preset_apply`).
     */
    external fun presetApply(
        names: Array<String>,
        folders: Array<String>,
        flags: IntArray,
        filterKind: Int,
        filterFolder: String?,
    ): IntArray

    /** The one row that gets the check, or -1 (`geode_preset_applied`). */
    external fun presetApplied(
        names: Array<String>,
        flags: IntArray,
        lastTapped: String?,
    ): Int

    /** A preset filter chip's kind (`GEODE_PRESET_FILTER_*`). */
    const val PRESET_FILTER_ALL: Int = 0
    const val PRESET_FILTER_THIS_STYLE: Int = 1
    const val PRESET_FILTER_MINE: Int = 2
    const val PRESET_FILTER_BUILT_IN: Int = 3
    const val PRESET_FILTER_FOLDER: Int = 4

    /** A preset row's marks, or-ed into its flags (`GEODE_PRESET_*`). */
    const val PRESET_BUILT_IN: Int = 1
    const val PRESET_FOR_THIS_STYLE: Int = 2
    const val PRESET_ON_STAGE: Int = 4
}
