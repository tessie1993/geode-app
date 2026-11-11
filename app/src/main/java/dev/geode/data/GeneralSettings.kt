package dev.geode.data

import android.content.SharedPreferences
import androidx.annotation.StringRes
import dev.geode.R
import dev.geode.data.prefs.PrefType
import dev.geode.data.prefs.PrefsCodec
import dev.geode.data.prefs.SettingsRepository
import dev.geode.data.prefs.prefKeys

enum class CornerStyle(
    @param:StringRes val labelRes: Int,
) {
    SHARP(R.string.look_corner_sharp),
    ROUNDED(R.string.look_corner_rounded),
    PILL(R.string.look_corner_pill),
}

/**
 * The app's own look and shell: what every screen draws with, the options glass, the mini player's
 * fade, picture-in-picture and interface sounds (#1009, PR 5; was part of `GuiPrefs`).
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

    /** The font colour before it was a colour; read while [KEY_FONT_COLOR] is unset, removed on write. */
    internal const val KEY_WHITE_FONT = "gui_white_font"

    /** Retired with the app's own reduced-motion switch (#336); removed on write. */
    internal const val KEY_REDUCED_MOTION = "gui_reduced_motion"

    /** Retired with the gem theme packs, their light variant, the background dim and the unread Visuals-menu switch. */
    private val RETIRED_THEME_KEYS = listOf("app_theme", "gui_background_dim", "gui_follow_system_dark", "gui_clear_visuals_menu")

    override val defaults = GeneralSettings()

    override val keys =
        prefKeys(GeodePrefsFiles.GENERAL) {
            portable(PrefType.STRING, KEY_CORNER)
            portable(PrefType.FLOAT, KEY_OPACITY, KEY_ACCENT, KEY_MINI_PLAYER_FADE, KEY_TEXT_SCALE)
            portable(PrefType.INT, KEY_FONT_COLOR)
            portable(PrefType.BOOLEAN, KEY_AUTO_ENTER_PIP, KEY_UI_SOUNDS)
            retired(PrefType.BOOLEAN, KEY_WHITE_FONT)
            retired(null, *RETIRED_THEME_KEYS.toTypedArray(), KEY_REDUCED_MOTION)
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
            .remove(KEY_WHITE_FONT)
            .remove(KEY_REDUCED_MOTION)
        val fontColor = value.fontColorArgb
        if (fontColor != null) editor.putInt(KEY_FONT_COLOR, fontColor) else editor.remove(KEY_FONT_COLOR)
        RETIRED_THEME_KEYS.forEach { editor.remove(it) }
    }
}
