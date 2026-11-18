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

    /**
     * An audio file another app opens in this one, "Open with" (core/shell/OpenedAudio). Routes a delivered
     * intent from its action, its data uri's scheme and last path segment and the sender's declared type:
     * one of [OPENED_NOT_OURS] and the rest (`geode_opened_audio_route`).
     */
    external fun openedAudioRoute(
        action: String?,
        scheme: String?,
        type: String?,
        lastSegment: String?,
        fromHistory: Boolean,
    ): Int

    /**
     * Whether the app must refuse to play the uri: [providerPackage] owns its authority (null when no provider
     * answers to it), [readGranted] says this app holds an explicit read grant on it (`geode_opened_audio_refuses`).
     */
    external fun openedAudioRefuses(
        scheme: String?,
        authority: String?,
        ownPackage: String,
        providerPackage: String?,
        readGranted: Boolean,
    ): Boolean

    /** The provider's display name made fit to show; null when nothing is left (`geode_opened_audio_display_name`). */
    external fun openedAudioDisplayName(raw: String?): String?

    /** A queue id for an opened file from 16 [random] bytes the caller drew (`geode_opened_audio_media_id`). */
    external fun openedAudioMediaId(random: ByteArray): String

    /** Whether a queue id marks an opened file (`geode_opened_audio_is_opened`). */
    external fun openedAudioIsOpened(mediaId: String?): Boolean

    /** [openedAudioRoute]'s answers (`GEODE_OPENED_*`). */
    const val OPENED_NOT_OURS: Int = 0
    const val OPENED_STALE: Int = 1
    const val OPENED_OPEN: Int = 2

    /**
     * Auto's remembered shape as the prefs keep it (core/shell/AutoVisuals). The entries a saved playlist text
     * holds, each scene id resolved by the catalogue, as [AUTO_ENTRY_FIELDS] texts per entry: scene id, preset
     * name, milk path, label; a null preset name or milk path is none. A null [json] is no entries
     * (`geode_auto_playlist_read`).
     */
    external fun autoPlaylistRead(json: String?): Array<String?>

    /** The playlist as the prefs keep it, from [AUTO_ENTRY_FIELDS] [fields] per entry (`geode_auto_playlist_to_json`). */
    external fun autoPlaylistToJson(fields: Array<String?>): String

    /**
     * The saved settings read over the state: [state] the [AUTO_SAVED_FIELDS] now, [stored] what the prefs hold
     * where [has], [playlistCount] the playlist's entries once read. The fields read: the timer off, an absent
     * value the state's, the playlist on only with entries, the intervals held to range (`geode_auto_visuals_apply`).
     */
    external fun autoVisualsApply(
        state: IntArray,
        stored: IntArray,
        has: BooleanArray,
        playlistCount: Int,
    ): IntArray

    /** Texts per playlist entry in [autoPlaylistRead] and [autoPlaylistToJson] (`GEODE_AUTO_ENTRY_FIELDS`). */
    const val AUTO_ENTRY_FIELDS: Int = 4

    /** [autoVisualsApply]'s fields, in order (`GEODE_AUTO_SAVED_*`); the booleans are 0 or 1. */
    const val AUTO_SAVED_RANDOM_ON: Int = 0
    const val AUTO_SAVED_RANDOM_INTERVAL: Int = 1
    const val AUTO_SAVED_RANDOM_STYLES: Int = 2
    const val AUTO_SAVED_RANDOM_PRESETS: Int = 3
    const val AUTO_SAVED_RANDOM_MILK: Int = 4
    const val AUTO_SAVED_PLAYLIST_ON: Int = 5
    const val AUTO_SAVED_PLAYLIST_INTERVAL: Int = 6
    const val AUTO_SAVED_FIELDS: Int = 7
}
