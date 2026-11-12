package dev.geode.data

import android.content.SharedPreferences
import androidx.annotation.StringRes
import dev.geode.R
import dev.geode.data.prefs.PrefType
import dev.geode.data.prefs.PrefsCodec
import dev.geode.data.prefs.SettingsRepository
import dev.geode.data.prefs.prefKeys
import dev.geode.model.KnobSize
import dev.geode.model.LibraryStartTab
import dev.geode.model.ScreenRotation
import dev.geode.model.TabLabels
import dev.geode.model.TrackTap

enum class CornerStyle(
    @param:StringRes val labelRes: Int,
) {
    SHARP(R.string.look_corner_sharp),
    ROUNDED(R.string.look_corner_rounded),
    PILL(R.string.look_corner_pill),
}

/**
 * The app's own look and shell: what every screen draws with, the options glass, the mini player's
 * fade, picture-in-picture and interface sounds, and Settings › Preferences' small choices for Library,
 * the players and the visual's icons (#1009, PR 5; was part of `GuiPrefs`).
 */
data class GeneralSettings(
    val cornerStyle: CornerStyle = CornerStyle.ROUNDED,
    /** The options glass over the visual (Visuals › Settings and Look). */
    val barOpacity: Float = 0.72f,
    val accentIntensity: Float = 1f,
    /**
     * How long the mini player stays after the last touch before it fades to invisible, in
     * seconds; 0 keeps it on screen.
     */
    val miniPlayerFadeSeconds: Float = MINI_PLAYER_FADE_DEFAULT_SECONDS,
    val fontColorArgb: Int? = null,
    val textScale: Float = 1f,
    /**
     * Whether leaving the fullscreen visualizer while something is playing drops straight into
     * picture-in-picture, instead of just backgrounding the app. Off by default, and the only way
     * into PiP: there is no PiP button.
     */
    val autoEnterPip: Boolean = false,
    /**
     * Interface sounds on taps and tab changes; still silent when the system's touch sounds are off.
     * Off on a fresh install (the owner's call): a music app should not add clicks over the music
     * until someone asks for them.
     */
    val uiSounds: Boolean = false,
    /** Off: the accent stays the fallback #9CC7FF instead of following the visual's palette. */
    val accentFollowsVisual: Boolean = true,
    /** The app's own vibration on taps; the system's touch feedback switch still applies on top. */
    val haptics: Boolean = true,
    val tabLabels: TabLabels = TabLabels.AUTO,
    /** Player shows the time left (−m:ss) where the track's length is, instead of the length. */
    val showTimeLeft: Boolean = false,
    val screenRotation: ScreenRotation = ScreenRotation.FOLLOW,
    val knobSize: KnobSize = KnobSize.SMALL,
    /** Settings › Preferences › Library: the tab Library opens on. */
    val libraryStartTab: LibraryStartTab = LibraryStartTab.TRACKS,
    /**
     * Settings › Preferences › Library: the order Library opens in, by `LibrarySort`'s name (that
     * enum is the screen's, and `data` may not name `ui`). Empty or unknown is the first order,
     * Title. A sort picked in Library itself still holds for the rest of the visit.
     */
    val librarySort: String = "",
    /** Settings › Preferences › Library: what a tap on a track does. */
    val trackTap: TrackTap = TrackTap.PLAY_LIST,
    /** Settings › Preferences › Player: the A–B loop button under the Player's seek bar. */
    val showAbLoop: Boolean = true,
    /** Settings › Preferences › Player: a previous button on the floating mini player, beside play and next. */
    val miniPlayerPrevious: Boolean = false,
    /** Settings › Preferences › Visuals: ‹ › at the sides of the visual (the sideways swipe still steps). */
    val stageArrows: Boolean = true,
    /** Settings › Preferences › Visuals: the Record icon on the visual (the hold still records; a running take keeps its stop). */
    val stageRecord: Boolean = true,
) {
    companion object {
        const val TEXT_SCALE_MIN = 0.85f
        const val TEXT_SCALE_MAX = 1.3f
        const val MINI_PLAYER_FADE_DEFAULT_SECONDS = 6f
        const val MINI_PLAYER_FADE_MAX_SECONDS = 10f
    }
}

typealias GeneralSettingsRepository = SettingsRepository<GeneralSettings>

/** How [GeneralSettings] sit in `geode-prefs`, under the keys `GuiPrefs` used. */
object GeneralSettingsCodec : PrefsCodec<GeneralSettings> {
    internal const val KEY_CORNER = "gui_corner"
    internal const val KEY_OPACITY = "gui_opacity"
    internal const val KEY_ACCENT = "gui_accent_intensity"
    internal const val KEY_MINI_PLAYER_FADE = "gui_mini_player_fade_sec"
    internal const val KEY_FONT_COLOR = "gui_font_color"
    internal const val KEY_TEXT_SCALE = "gui_text_scale"
    internal const val KEY_AUTO_ENTER_PIP = "gui_auto_enter_pip"
    internal const val KEY_UI_SOUNDS = "gui_ui_sounds"
    internal const val KEY_ACCENT_FOLLOWS_VISUAL = "gui_accent_follows_visual"
    internal const val KEY_HAPTICS = "gui_haptics"
    internal const val KEY_TAB_LABELS = "gui_tab_labels"
    internal const val KEY_SHOW_TIME_LEFT = "gui_show_time_left"
    internal const val KEY_SCREEN_ROTATION = "gui_screen_rotation"
    internal const val KEY_KNOB_SIZE = "gui_knob_size"
    internal const val KEY_LIBRARY_START_TAB = "gui_library_start_tab"
    internal const val KEY_LIBRARY_SORT = "gui_library_sort"
    internal const val KEY_TRACK_TAP = "gui_track_tap"
    internal const val KEY_SHOW_AB_LOOP = "gui_show_ab_loop"
    internal const val KEY_MINI_PLAYER_PREVIOUS = "gui_mini_player_previous"
    internal const val KEY_STAGE_ARROWS = "gui_stage_arrows"
    internal const val KEY_STAGE_RECORD = "gui_stage_record"

    /** The font colour before it was a colour; read while [KEY_FONT_COLOR] is unset, removed on write. */
    internal const val KEY_WHITE_FONT = "gui_white_font"

    /** Retired with the app's own reduced-motion switch (#336); removed on write. */
    internal const val KEY_REDUCED_MOTION = "gui_reduced_motion"

    /** Retired with the gem theme packs, their light variant, the background dim and the unread Visuals-menu switch. */
    private val RETIRED_THEME_KEYS = listOf("app_theme", "gui_background_dim", "gui_follow_system_dark", "gui_clear_visuals_menu")

    override val defaults = GeneralSettings()

    override val keys =
        prefKeys(GeodePrefsFiles.GENERAL) {
            portable(PrefType.STRING, KEY_CORNER, KEY_TAB_LABELS, KEY_SCREEN_ROTATION, KEY_KNOB_SIZE)
            portable(PrefType.FLOAT, KEY_OPACITY, KEY_ACCENT, KEY_MINI_PLAYER_FADE, KEY_TEXT_SCALE)
            portable(PrefType.INT, KEY_FONT_COLOR)
            portable(PrefType.BOOLEAN, KEY_AUTO_ENTER_PIP, KEY_UI_SOUNDS)
            portable(PrefType.BOOLEAN, KEY_ACCENT_FOLLOWS_VISUAL, KEY_HAPTICS, KEY_SHOW_TIME_LEFT)
            portable(PrefType.STRING, KEY_LIBRARY_START_TAB, KEY_LIBRARY_SORT, KEY_TRACK_TAP)
            portable(PrefType.BOOLEAN, KEY_SHOW_AB_LOOP, KEY_MINI_PLAYER_PREVIOUS, KEY_STAGE_ARROWS, KEY_STAGE_RECORD)
            retired(PrefType.BOOLEAN, KEY_WHITE_FONT)
            RETIRED_THEME_KEYS.forEach { retired(null, it) }
            retired(null, KEY_REDUCED_MOTION)
        }

    override fun read(prefs: SharedPreferences): GeneralSettings {
        val d = defaults
        return GeneralSettings(
            cornerStyle = CornerStyle.entries.firstOrNull { it.name == prefs.getString(KEY_CORNER, null) } ?: d.cornerStyle,
            barOpacity = prefs.getFloat(KEY_OPACITY, d.barOpacity),
            accentIntensity = prefs.getFloat(KEY_ACCENT, d.accentIntensity),
            miniPlayerFadeSeconds = prefs.getFloat(KEY_MINI_PLAYER_FADE, d.miniPlayerFadeSeconds),
            fontColorArgb =
                when {
                    prefs.contains(KEY_FONT_COLOR) -> prefs.getInt(KEY_FONT_COLOR, 0)
                    prefs.getBoolean(KEY_WHITE_FONT, false) -> FontColorChoice.WHITE_ARGB
                    else -> null
                },
            textScale = prefs.getFloat(KEY_TEXT_SCALE, d.textScale),
            autoEnterPip = prefs.getBoolean(KEY_AUTO_ENTER_PIP, d.autoEnterPip),
            uiSounds = prefs.getBoolean(KEY_UI_SOUNDS, d.uiSounds),
            accentFollowsVisual = prefs.getBoolean(KEY_ACCENT_FOLLOWS_VISUAL, d.accentFollowsVisual),
            haptics = prefs.getBoolean(KEY_HAPTICS, d.haptics),
            tabLabels = TabLabels.fromName(prefs.getString(KEY_TAB_LABELS, null)),
            showTimeLeft = prefs.getBoolean(KEY_SHOW_TIME_LEFT, d.showTimeLeft),
            screenRotation = ScreenRotation.fromName(prefs.getString(KEY_SCREEN_ROTATION, null)),
            knobSize = KnobSize.fromName(prefs.getString(KEY_KNOB_SIZE, null)),
            libraryStartTab = LibraryStartTab.fromName(prefs.getString(KEY_LIBRARY_START_TAB, null)),
            librarySort = prefs.getString(KEY_LIBRARY_SORT, null).orEmpty(),
            trackTap = TrackTap.fromName(prefs.getString(KEY_TRACK_TAP, null)),
            showAbLoop = prefs.getBoolean(KEY_SHOW_AB_LOOP, d.showAbLoop),
            miniPlayerPrevious = prefs.getBoolean(KEY_MINI_PLAYER_PREVIOUS, d.miniPlayerPrevious),
            stageArrows = prefs.getBoolean(KEY_STAGE_ARROWS, d.stageArrows),
            stageRecord = prefs.getBoolean(KEY_STAGE_RECORD, d.stageRecord),
        )
    }

    override fun normalize(value: GeneralSettings): GeneralSettings =
        value.copy(
            miniPlayerFadeSeconds = value.miniPlayerFadeSeconds.coerceIn(0f, GeneralSettings.MINI_PLAYER_FADE_MAX_SECONDS),
            textScale = value.textScale.coerceIn(GeneralSettings.TEXT_SCALE_MIN, GeneralSettings.TEXT_SCALE_MAX),
        )

    override fun write(
        editor: SharedPreferences.Editor,
        value: GeneralSettings,
    ) {
        editor
            .putString(KEY_CORNER, value.cornerStyle.name)
            .putFloat(KEY_OPACITY, value.barOpacity)
            .putFloat(KEY_ACCENT, value.accentIntensity)
            .putFloat(KEY_MINI_PLAYER_FADE, value.miniPlayerFadeSeconds)
            .putFloat(KEY_TEXT_SCALE, value.textScale)
            .putBoolean(KEY_AUTO_ENTER_PIP, value.autoEnterPip)
            .putBoolean(KEY_UI_SOUNDS, value.uiSounds)
            .putBoolean(KEY_ACCENT_FOLLOWS_VISUAL, value.accentFollowsVisual)
            .putBoolean(KEY_HAPTICS, value.haptics)
            .putString(KEY_TAB_LABELS, value.tabLabels.name)
            .putBoolean(KEY_SHOW_TIME_LEFT, value.showTimeLeft)
            .putString(KEY_SCREEN_ROTATION, value.screenRotation.name)
            .putString(KEY_KNOB_SIZE, value.knobSize.name)
            .putString(KEY_LIBRARY_START_TAB, value.libraryStartTab.name)
            .putString(KEY_LIBRARY_SORT, value.librarySort)
            .putString(KEY_TRACK_TAP, value.trackTap.name)
            .putBoolean(KEY_SHOW_AB_LOOP, value.showAbLoop)
            .putBoolean(KEY_MINI_PLAYER_PREVIOUS, value.miniPlayerPrevious)
            .putBoolean(KEY_STAGE_ARROWS, value.stageArrows)
            .putBoolean(KEY_STAGE_RECORD, value.stageRecord)
            .remove(KEY_WHITE_FONT)
            .remove(KEY_REDUCED_MOTION)
        val fontColor = value.fontColorArgb
        if (fontColor != null) editor.putInt(KEY_FONT_COLOR, fontColor) else editor.remove(KEY_FONT_COLOR)
        RETIRED_THEME_KEYS.forEach { editor.remove(it) }
    }
}
