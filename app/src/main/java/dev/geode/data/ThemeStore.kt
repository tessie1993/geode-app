package dev.geode.data

import android.content.SharedPreferences
import androidx.annotation.StringRes
import androidx.core.content.edit
import dev.geode.R
import dev.geode.data.prefs.BackupClass
import dev.geode.data.prefs.PrefType
import dev.geode.data.prefs.prefKeys
import dev.geode.engine.audio.SoundTuning
import dev.geode.model.TourProgress

enum class CornerStyle(
    @param:StringRes val labelRes: Int,
) {
    SHARP(R.string.look_corner_sharp),
    ROUNDED(R.string.look_corner_rounded),
    PILL(R.string.look_corner_pill),
}

data class GuiPrefs(
    val cornerStyle: CornerStyle = CornerStyle.ROUNDED,
    val barOpacity: Float = 0.72f,
    /** The sound engine's tuning (Settings › Audio): every surface and the export read it. */
    val soundTuning: SoundTuning = SoundTuning(),
    val presetMirrorUri: String? = null,
    /**
     * How long a preset load takes to glide in, in SECONDS.
     *
     * A duration, never a count of beats: nothing here consults or assumes a tempo.
     */
    val presetMorphSeconds: Float = 2f,
    val accentIntensity: Float = 1f,
    /**
     * How long the mini player on Visuals stays after the last touch before it fades to invisible,
     * in seconds; 0 keeps it on screen. Three seconds shows the track long enough to read, then
     * gives the canvas back.
     */
    val miniPlayerFadeSeconds: Float = MINI_PLAYER_FADE_DEFAULT_SECONDS,
    val whiteFont: Boolean = false,
    val fontColorArgb: Int? = null,
    val textScale: Float = 1f,
    /**
     * Whether the photosensitivity notice has been seen. It is an acknowledgement, not a choice:
     * the flash clamp is unconditional either way, so this only decides whether the notice shows.
     */
    val safetyAcknowledged: Boolean = false,
    /**
     * Whether first-run setup has been completed.
     *
     * This used to be inferred from `intent == null`, back when setup ended by asking what the
     * person came here to do. That question is gone — it made someone categorise themselves
     * before they had seen anything to categorise — so the flag is now explicit rather than a
     * side effect of an answer.
     */
    val setupDone: Boolean = false,
    /** The tour: chapters seen or skipped, where an unfinished one picks up, the tips said. */
    val tourProgress: TourProgress = TourProgress(),
    val micReactive: Boolean = false,
    val touchSmear: Boolean = false,
    val touchSmearStrength: Float = 1f,
    val secondScreen: Boolean = true,
    val touchTransform: Boolean = true,
    /**
     * Whether leaving the fullscreen visualizer while something is playing drops straight into
     * picture-in-picture, instead of just backgrounding the app. Off by default: PiP is a
     * deliberate action (the button on the visualizer, or the platform's own gesture on API 31+)
     * until someone opts into it happening automatically too.
     */
    val autoEnterPip: Boolean = false,
    /**
     * Interface sounds on taps and tab changes; still silent when the system's touch sounds are off.
     * Off on a fresh install (the owner's call): a music app should not add clicks over the music
     * until someone asks for them.
     */
    val uiSounds: Boolean = false,
) {
    val fontColorOverride: Int?
        get() = fontColorArgb ?: FontColorChoice.WHITE_ARGB.takeIf { whiteFont }

    companion object {
        const val TEXT_SCALE_MIN = 0.85f
        const val TEXT_SCALE_MAX = 1.3f
        const val MINI_PLAYER_FADE_DEFAULT_SECONDS = 6f
        const val MINI_PLAYER_FADE_MAX_SECONDS = 10f
    }
}

class ThemeStore(
    private val prefs: SharedPreferences,
) {
    private fun readPresetMorphSeconds(prefs: SharedPreferences): Float =
        prefs.getFloat(KEY_PRESET_MORPH_SEC, 2f).coerceIn(0f, PRESET_MORPH_SECONDS_MAX)

    fun loadGui(): GuiPrefs {
        val fontColor =
            when {
                prefs.contains(KEY_FONT_COLOR) -> prefs.getInt(KEY_FONT_COLOR, 0)
                prefs.getBoolean(KEY_WHITE_FONT, false) -> FontColorChoice.WHITE_ARGB
                else -> null
            }
        return GuiPrefs(
            cornerStyle =
                runCatching { CornerStyle.valueOf(prefs.getString(KEY_CORNER, null) ?: CornerStyle.ROUNDED.name) }
                    .getOrDefault(CornerStyle.ROUNDED),
            barOpacity = prefs.getFloat(KEY_OPACITY, 0.72f),
            soundTuning = readSoundTuning(prefs),
            presetMirrorUri = prefs.getString("preset_mirror_uri", null),
            presetMorphSeconds = readPresetMorphSeconds(prefs),
            accentIntensity = prefs.getFloat(KEY_ACCENT, 1f),
            miniPlayerFadeSeconds =
                prefs
                    .getFloat(KEY_MINI_PLAYER_FADE, GuiPrefs.MINI_PLAYER_FADE_DEFAULT_SECONDS)
                    .coerceIn(0f, GuiPrefs.MINI_PLAYER_FADE_MAX_SECONDS),
            fontColorArgb = fontColor,
            textScale =
                prefs
                    .getFloat(KEY_TEXT_SCALE, 1f)
                    .coerceIn(GuiPrefs.TEXT_SCALE_MIN, GuiPrefs.TEXT_SCALE_MAX),
            safetyAcknowledged = loadSafetyAcknowledged(),
            setupDone = loadSetupDone(),
            tourProgress =
                TourProgress.decode(
                    done = prefs.getString(KEY_TUTORIAL_DONE, null),
                    resume = prefs.getString(KEY_TUTORIAL_RESUME, null),
                    tips = prefs.getString(KEY_TUTORIAL_TIPS, null),
                    legacySeen = prefs.getBoolean(KEY_TUTORIAL_SEEN, false),
                ),
            touchSmear = prefs.getBoolean(KEY_TOUCH_SMEAR, false),
            touchSmearStrength = prefs.getFloat(KEY_TOUCH_SMEAR_STRENGTH, 1f).coerceIn(0.2f, 2f),
            touchTransform = prefs.getBoolean(KEY_TOUCH_TRANSFORM, true),
            secondScreen = prefs.getBoolean(KEY_SECOND_SCREEN, true),
            micReactive = prefs.getBoolean(KEY_MIC_REACTIVE, false),
            autoEnterPip = prefs.getBoolean(KEY_AUTO_ENTER_PIP, false),
            uiSounds = prefs.getBoolean(KEY_UI_SOUNDS, false),
        )
    }

    /**
     * Anyone who answered the old three-way safety question has already been shown the notice, so
     * they are not asked again — the question itself is gone, but the acknowledgement carries over.
     */
    private fun loadSafetyAcknowledged(): Boolean =
        prefs.getBoolean(KEY_SAFETY_ACKNOWLEDGED, false) ||
            prefs.getString(KEY_SAFETY_CHOICE, null) != null

    /**
     * Anyone who answered the old "what did you come here to do" question had, by answering it,
     * finished setup — so they are not walked through it again. The question is gone; the fact
     * that they got past it carries over.
     *
     * Only while the explicit flag has never been written, though: older builds wrote the intent on
     * every save, so once one had run, a stored intent no longer meant anyone answered anything. Read
     * it the other way and acknowledging the safety notice mid-setup would skip the rest of setup on
     * the next launch.
     */
    private fun loadSetupDone(): Boolean =
        if (prefs.contains(KEY_SETUP_DONE)) {
            prefs.getBoolean(KEY_SETUP_DONE, false)
        } else {
            prefs.getString(KEY_INTENT, null) != null
        }

    fun saveGui(gui: GuiPrefs) {
        val fontColor = gui.fontColorOverride
        prefs.edit {
            putString(KEY_CORNER, gui.cornerStyle.name)
            putFloat(KEY_OPACITY, gui.barOpacity)
            // The engine's defaults save as no key, so they follow native's schema when it changes.
            val tuning = gui.soundTuning.savedFloats()
            if (tuning == null) remove(KEY_SOUND_TUNING) else putString(KEY_SOUND_TUNING, tuning.joinToString(","))
            putString("preset_mirror_uri", gui.presetMirrorUri)
            putFloat(KEY_PRESET_MORPH_SEC, gui.presetMorphSeconds)
            putFloat(KEY_ACCENT, gui.accentIntensity)
            putFloat(KEY_MINI_PLAYER_FADE, gui.miniPlayerFadeSeconds)
            // Dead: no screen read the "clear Visuals menu" switch.
            remove(KEY_CLEAR_VIZ_MENU)
            if (fontColor != null) putInt(KEY_FONT_COLOR, fontColor) else remove(KEY_FONT_COLOR)
            remove(KEY_WHITE_FONT)
            // The gem theme packs, their light variant and the background dim are gone.
            remove(KEY)
            remove(KEY_DIM)
            remove(KEY_FOLLOW_DARK)
            putFloat(KEY_TEXT_SCALE, gui.textScale)
            putBoolean(KEY_SAFETY_ACKNOWLEDGED, gui.safetyAcknowledged)
            putBoolean(KEY_SETUP_DONE, gui.setupDone)
            // The old "tour ended" flag is folded into the chapters on the first save after it is read.
            remove(KEY_TUTORIAL_SEEN)
            remove(KEY_TUTORIAL_ON_FIRST_RUN)
            putString(KEY_TUTORIAL_DONE, gui.tourProgress.encodeDone())
            putString(KEY_TUTORIAL_RESUME, gui.tourProgress.encodeResume())
            putString(KEY_TUTORIAL_TIPS, gui.tourProgress.encodeTips())
            // Reduced motion follows Android's "Remove animations" alone now (data/SystemMotion,
            // #336); the app's own switch is gone, so its old answer is dropped on the next save.
            remove(KEY_REDUCED_MOTION)
            putBoolean(KEY_TOUCH_SMEAR, gui.touchSmear)
            putFloat(KEY_TOUCH_SMEAR_STRENGTH, gui.touchSmearStrength)
            putBoolean(KEY_TOUCH_TRANSFORM, gui.touchTransform)
            putBoolean(KEY_SECOND_SCREEN, gui.secondScreen)
            putBoolean(KEY_MIC_REACTIVE, gui.micReactive)
            putBoolean(KEY_AUTO_ENTER_PIP, gui.autoEnterPip)
            putBoolean(KEY_UI_SOUNDS, gui.uiSounds)
        }
    }

    internal companion object {
        const val KEY_SAFETY_ACKNOWLEDGED = "gui_safety_acknowledged"

        /** Read-only now: the retired first-run answer, kept solely so an old install counts as set up. */
        const val KEY_INTENT = "gui_intent"
        const val KEY_SETUP_DONE = "gui_setup_done"
        const val KEY_TUTORIAL_SEEN = "gui_tutorial_seen"
        const val KEY_TUTORIAL_ON_FIRST_RUN = "gui_tutorial_on_first_run"
        const val KEY_TUTORIAL_DONE = "gui_tutorial_done"
        const val KEY_TUTORIAL_RESUME = "gui_tutorial_resume"
        const val KEY_TUTORIAL_TIPS = "gui_tutorial_tips"

        /** Read-only now: the old three-way answer, kept solely to migrate existing installs. */
        const val KEY_SAFETY_CHOICE = "gui_safety_choice"

        /** Retired with the gem theme packs; removed on the next save. */
        const val KEY = "app_theme"
        const val KEY_CORNER = "gui_corner"
        const val KEY_OPACITY = "gui_opacity"
        const val KEY_ACCENT = "gui_accent_intensity"
        const val KEY_DIM = "gui_background_dim"
        const val KEY_FOLLOW_DARK = "gui_follow_system_dark"

        /** Retired: written, never read. Removed on the next save. */
        const val KEY_CLEAR_VIZ_MENU = "gui_clear_visuals_menu"
        const val KEY_MINI_PLAYER_FADE = "gui_mini_player_fade_sec"

        const val KEY_WHITE_FONT = "gui_white_font"
        const val KEY_FONT_COLOR = "gui_font_color"
        const val KEY_TEXT_SCALE = "gui_text_scale"
        const val KEY_SOUND_TUNING = "sound_tuning"

        /** The saved tuning, or the engine's defaults when there is none or it does not parse. */
        fun readSoundTuning(prefs: SharedPreferences): SoundTuning {
            val floats =
                prefs
                    .getString(KEY_SOUND_TUNING, null)
                    ?.split(',')
                    ?.map { it.toFloatOrNull() ?: return SoundTuning() }
                    ?.toFloatArray() ?: return SoundTuning()
            return SoundTuning.fromFloats(floats)
        }

        const val KEY_PRESET_MORPH_SEC = "gui_preset_morph_seconds"

        const val PRESET_MORPH_SECONDS_MAX = 8f

        /** Retired with the app's own reduced-motion switch (#336); removed on the next save. */
        const val KEY_REDUCED_MOTION = "gui_reduced_motion"
        const val KEY_TOUCH_SMEAR = "gui_touch_smear"
        const val KEY_TOUCH_SMEAR_STRENGTH = "gui_touch_smear_strength"
        const val KEY_TOUCH_TRANSFORM = "gui_touch_transform"
        const val KEY_SECOND_SCREEN = "gui_second_screen"
        const val KEY_MIC_REACTIVE = "gui_mic_reactive"
        const val KEY_AUTO_ENTER_PIP = "gui_auto_enter_pip"
        const val KEY_UI_SOUNDS = "gui_ui_sounds"

        val PREF_KEYS =
            prefKeys(GeodePrefsFiles.GENERAL) {
                portable(PrefType.STRING, KEY_CORNER, KEY_SOUND_TUNING)
                portable(PrefType.FLOAT, KEY_OPACITY, KEY_PRESET_MORPH_SEC, KEY_ACCENT, KEY_MINI_PLAYER_FADE, KEY_TEXT_SCALE)
                portable(PrefType.FLOAT, KEY_TOUCH_SMEAR_STRENGTH)
                portable(PrefType.INT, KEY_FONT_COLOR)
                portable(PrefType.BOOLEAN, KEY_TOUCH_SMEAR, KEY_TOUCH_TRANSFORM, KEY_SECOND_SCREEN, KEY_MIC_REACTIVE)
                portable(PrefType.BOOLEAN, KEY_AUTO_ENTER_PIP, KEY_UI_SOUNDS)
                device(PrefType.STRING, "preset_mirror_uri")
                state(PrefType.BOOLEAN, KEY_SAFETY_ACKNOWLEDGED, KEY_SETUP_DONE)
                state(PrefType.STRING, KEY_TUTORIAL_DONE, KEY_TUTORIAL_RESUME, KEY_TUTORIAL_TIPS)
                retired(PrefType.STRING, KEY_INTENT, KEY_SAFETY_CHOICE)
                retired(PrefType.BOOLEAN, KEY_WHITE_FONT)
                // Read once to fold an old install's "tour ended" into the chapters, then removed.
                retired(PrefType.BOOLEAN, KEY_TUTORIAL_SEEN, backup = BackupClass.STATE)
                // The morph length in beats: no longer read (the tempo is not a setting), still in old installs' files.
                retired(PrefType.INT, "morph_beats")
                retired(null, KEY, KEY_DIM, KEY_FOLLOW_DARK, KEY_CLEAR_VIZ_MENU, KEY_TUTORIAL_ON_FIRST_RUN, KEY_REDUCED_MOTION)
            }
    }
}
