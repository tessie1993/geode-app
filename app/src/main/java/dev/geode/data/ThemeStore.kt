package dev.geode.data

import android.content.SharedPreferences
import androidx.annotation.StringRes
import androidx.core.content.edit
import dev.geode.R
import dev.geode.analysis.BeatTuning
import dev.geode.engine.audio.SoundTuning
import dev.geode.model.TutorialProgress

enum class PlayerPosition(
    @param:StringRes val labelRes: Int,
) {
    TOP(R.string.look_position_top),
    BOTTOM(R.string.look_position_bottom),
}

enum class CornerStyle(
    @param:StringRes val labelRes: Int,
) {
    SHARP(R.string.look_corner_sharp),
    ROUNDED(R.string.look_corner_rounded),
    PILL(R.string.look_corner_pill),
}

data class GuiPrefs(
    val playerPosition: PlayerPosition = PlayerPosition.BOTTOM,
    val cornerStyle: CornerStyle = CornerStyle.ROUNDED,
    val barOpacity: Float = 0.72f,
    val beatSensitivity: Float = BeatTuning.SENSITIVITY_DEFAULT,
    val beatMinIntervalMs: Float = BeatTuning.INTERVAL_MS_DEFAULT,
    /** The sound engine's tuning (Settings › Audio): every surface and the export read it. */
    val soundTuning: SoundTuning = SoundTuning(),
    val presetMirrorUri: String? = null,
    /**
     * How long a preset load takes to glide in, in SECONDS.
     *
     * Was a count of beats, converted with the tracked tempo — so the glide was the wrong
     * length whenever the estimate was, and fell back to a flat 120 BPM on live input where
     * there is no estimate at all. Seconds are the thing the user is actually choosing.
     */
    val presetMorphSeconds: Float = 2f,
    val accentIntensity: Float = 1f,
    val compactPlayer: Boolean = false,
    val clearVisualsMenu: Boolean = false,
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
    /**
     * What the person came here to do: which tab the app opens on, and whether Studio appears
     * in the navigation at all.
     *
     * No longer asked at first run. Everyone starts on [UserIntent.BOTH], which shows
     * everything, and anyone who wants a narrower app says so in Settings once they know what
     * they would be narrowing.
     */
    val intent: UserIntent = UserIntent.BOTH,
    /** Whether the walkthrough has been finished or skipped at least once. */
    val tutorialSeen: Boolean = false,
    /**
     * The chapters finished (Settings › Tutorials ticks) and where the first-run tour picks up if
     * the app was closed mid-tour. Kept apart from [tutorialSeen], which only says the first-run
     * tour has ended (finished or Skip all) and so is not offered again.
     */
    val tutorialProgress: TutorialProgress = TutorialProgress(),
    val reducedMotion: Boolean = false,
    val micReactive: Boolean = false,
    val touchSmear: Boolean = false,
    val touchSmearStrength: Float = 1f,
    val keyColor: Boolean = false,
    val secondScreen: Boolean = true,
    val touchTransform: Boolean = true,
    /**
     * Whether leaving the fullscreen visualizer while something is playing drops straight into
     * picture-in-picture, instead of just backgrounding the app. Off by default: PiP is a
     * deliberate action (the button on the visualizer, or the platform's own gesture on API 31+)
     * until someone opts into it happening automatically too.
     */
    val autoEnterPip: Boolean = false,
    /** Interface sounds on taps and tab changes; still silent when the system's touch sounds are off. */
    val uiSounds: Boolean = true,
) {
    val fontColorOverride: Int?
        get() = fontColorArgb ?: FontColorChoice.WHITE_ARGB.takeIf { whiteFont }

    val effectiveBeatMinIntervalMs: Float
        get() =
            dev.geode.render.VisualSafety
                .beatMinIntervalMs(beatMinIntervalMs)

    companion object {
        const val TEXT_SCALE_MIN = 0.85f
        const val TEXT_SCALE_MAX = 1.3f
    }
}

class ThemeStore(
    private val prefs: SharedPreferences,
) {
    /** Reads the morph length, converting a beat count saved by an older build. */
    private fun readPresetMorphSeconds(prefs: SharedPreferences): Float {
        if (prefs.contains(KEY_PRESET_MORPH_SEC)) {
            return prefs.getFloat(KEY_PRESET_MORPH_SEC, 2f).coerceIn(0f, PRESET_MORPH_SECONDS_MAX)
        }
        val beats = prefs.getInt(LEGACY_KEY_MORPH_BEATS, 4)
        return (beats * 60f / LEGACY_MORPH_BPM).coerceIn(0f, PRESET_MORPH_SECONDS_MAX)
    }

    fun loadGui(): GuiPrefs {
        val fontColor =
            when {
                prefs.contains(KEY_FONT_COLOR) -> prefs.getInt(KEY_FONT_COLOR, 0)
                prefs.getBoolean(KEY_WHITE_FONT, false) -> FontColorChoice.WHITE_ARGB
                else -> null
            }
        return GuiPrefs(
            playerPosition =
                runCatching { PlayerPosition.valueOf(prefs.getString(KEY_POS, null) ?: PlayerPosition.BOTTOM.name) }
                    .getOrDefault(PlayerPosition.BOTTOM),
            cornerStyle =
                runCatching { CornerStyle.valueOf(prefs.getString(KEY_CORNER, null) ?: CornerStyle.ROUNDED.name) }
                    .getOrDefault(CornerStyle.ROUNDED),
            barOpacity = prefs.getFloat(KEY_OPACITY, 0.72f),
            beatSensitivity =
                prefs
                    .getFloat("beat_sigma", BeatTuning.SENSITIVITY_DEFAULT)
                    .coerceIn(BeatTuning.SENSITIVITY_MIN, BeatTuning.SENSITIVITY_MAX),
            beatMinIntervalMs =
                prefs
                    .getFloat(KEY_BEAT_INTERVAL, BeatTuning.INTERVAL_MS_DEFAULT)
                    .coerceIn(BeatTuning.INTERVAL_MS_MIN, BeatTuning.INTERVAL_MS_MAX),
            soundTuning = readSoundTuning(prefs),
            presetMirrorUri = prefs.getString("preset_mirror_uri", null),
            presetMorphSeconds = readPresetMorphSeconds(prefs),
            accentIntensity = prefs.getFloat(KEY_ACCENT, 1f),
            compactPlayer = prefs.getBoolean(KEY_COMPACT, false),
            clearVisualsMenu = prefs.getBoolean(KEY_CLEAR_VIZ_MENU, false),
            fontColorArgb = fontColor,
            textScale =
                prefs
                    .getFloat(KEY_TEXT_SCALE, 1f)
                    .coerceIn(GuiPrefs.TEXT_SCALE_MIN, GuiPrefs.TEXT_SCALE_MAX),
            safetyAcknowledged = loadSafetyAcknowledged(),
            setupDone = loadSetupDone(),
            intent =
                prefs
                    .getString(KEY_INTENT, null)
                    ?.let { stored -> UserIntent.entries.firstOrNull { it.name == stored } }
                    ?: UserIntent.BOTH,
            tutorialSeen = prefs.getBoolean(KEY_TUTORIAL_SEEN, false),
            tutorialProgress =
                TutorialProgress.decode(
                    prefs.getString(KEY_TUTORIAL_DONE, null),
                    prefs.getString(KEY_TUTORIAL_RESUME, null),
                ),
            reducedMotion = loadReducedMotion(),
            touchSmear = prefs.getBoolean(KEY_TOUCH_SMEAR, false),
            touchSmearStrength = prefs.getFloat(KEY_TOUCH_SMEAR_STRENGTH, 1f).coerceIn(0.2f, 2f),
            touchTransform = prefs.getBoolean(KEY_TOUCH_TRANSFORM, true),
            keyColor = prefs.getBoolean(KEY_KEY_COLOR, false),
            secondScreen = prefs.getBoolean(KEY_SECOND_SCREEN, true),
            micReactive = prefs.getBoolean(KEY_MIC_REACTIVE, false),
            autoEnterPip = prefs.getBoolean(KEY_AUTO_ENTER_PIP, false),
            uiSounds = prefs.getBoolean(KEY_UI_SOUNDS, true),
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
     * Only while the explicit flag has never been written, though: [saveGui] writes the intent on
     * every save, so once it has run, a stored intent no longer means anyone answered anything. Read
     * it the other way and acknowledging the safety notice mid-setup would skip the rest of setup on
     * the next launch.
     */
    private fun loadSetupDone(): Boolean =
        if (prefs.contains(KEY_SETUP_DONE)) {
            prefs.getBoolean(KEY_SETUP_DONE, false)
        } else {
            prefs.getString(KEY_INTENT, null) != null
        }

    /**
     * Reduced motion survives the same migration: it used to be one of the three answers, and it is
     * the only one of them that described something other than the flash clamp. The old answer is
     * never deleted, so it seeds the setting only until the setting has been saved once; after that
     * turning reduced motion off has to stick.
     */
    private fun loadReducedMotion(): Boolean =
        if (prefs.contains(KEY_REDUCED_MOTION)) {
            prefs.getBoolean(KEY_REDUCED_MOTION, false)
        } else {
            prefs.getString(KEY_SAFETY_CHOICE, null) == LEGACY_CHOICE_REDUCED_MOTION
        }

    fun saveGui(gui: GuiPrefs) {
        val fontColor = gui.fontColorOverride
        prefs.edit {
            putString(KEY_POS, gui.playerPosition.name)
            putString(KEY_CORNER, gui.cornerStyle.name)
            putFloat(KEY_OPACITY, gui.barOpacity)
            putFloat("beat_sigma", gui.beatSensitivity)
            putFloat(KEY_BEAT_INTERVAL, gui.beatMinIntervalMs)
            putString(KEY_SOUND_TUNING, gui.soundTuning.toFloats().joinToString(","))
            putString("preset_mirror_uri", gui.presetMirrorUri)
            putFloat(KEY_PRESET_MORPH_SEC, gui.presetMorphSeconds)
            putFloat(KEY_ACCENT, gui.accentIntensity)
            putBoolean(KEY_COMPACT, gui.compactPlayer)
            putBoolean(KEY_CLEAR_VIZ_MENU, gui.clearVisualsMenu)
            if (fontColor != null) putInt(KEY_FONT_COLOR, fontColor) else remove(KEY_FONT_COLOR)
            remove(KEY_WHITE_FONT)
            // The gem theme packs, their light variant and the background dim are gone.
            remove(KEY)
            remove(KEY_DIM)
            remove(KEY_FOLLOW_DARK)
            putFloat(KEY_TEXT_SCALE, gui.textScale)
            putBoolean(KEY_SAFETY_ACKNOWLEDGED, gui.safetyAcknowledged)
            putBoolean(KEY_SETUP_DONE, gui.setupDone)
            putString(KEY_INTENT, gui.intent.name)
            putBoolean(KEY_TUTORIAL_SEEN, gui.tutorialSeen)
            // The old "offer it on a fresh install" switch is gone: the tour offers itself once.
            remove(KEY_TUTORIAL_ON_FIRST_RUN)
            putString(KEY_TUTORIAL_DONE, gui.tutorialProgress.encodeDone())
            putString(KEY_TUTORIAL_RESUME, gui.tutorialProgress.encodeResume())
            putBoolean(KEY_REDUCED_MOTION, gui.reducedMotion)
            putBoolean(KEY_TOUCH_SMEAR, gui.touchSmear)
            putFloat(KEY_TOUCH_SMEAR_STRENGTH, gui.touchSmearStrength)
            putBoolean(KEY_TOUCH_TRANSFORM, gui.touchTransform)
            putBoolean(KEY_KEY_COLOR, gui.keyColor)
            putBoolean(KEY_SECOND_SCREEN, gui.secondScreen)
            putBoolean(KEY_MIC_REACTIVE, gui.micReactive)
            putBoolean(KEY_AUTO_ENTER_PIP, gui.autoEnterPip)
            putBoolean(KEY_UI_SOUNDS, gui.uiSounds)
        }
    }

    internal companion object {
        const val KEY_SAFETY_ACKNOWLEDGED = "gui_safety_acknowledged"
        const val KEY_INTENT = "gui_intent"
        const val KEY_SETUP_DONE = "gui_setup_done"
        const val KEY_TUTORIAL_SEEN = "gui_tutorial_seen"
        const val KEY_TUTORIAL_ON_FIRST_RUN = "gui_tutorial_on_first_run"
        const val KEY_TUTORIAL_DONE = "gui_tutorial_done"
        const val KEY_TUTORIAL_RESUME = "gui_tutorial_resume"

        /** Read-only now: the old three-way answer, kept solely to migrate existing installs. */
        const val KEY_SAFETY_CHOICE = "gui_safety_choice"
        const val LEGACY_CHOICE_REDUCED_MOTION = "REDUCED_MOTION"

        /** Retired with the gem theme packs; removed on the next save. */
        const val KEY = "app_theme"
        const val KEY_POS = "gui_player_pos"
        const val KEY_CORNER = "gui_corner"
        const val KEY_OPACITY = "gui_opacity"
        const val KEY_ACCENT = "gui_accent_intensity"
        const val KEY_DIM = "gui_background_dim"
        const val KEY_COMPACT = "gui_compact_player"
        const val KEY_FOLLOW_DARK = "gui_follow_system_dark"
        const val KEY_CLEAR_VIZ_MENU = "gui_clear_visuals_menu"

        const val KEY_WHITE_FONT = "gui_white_font"
        const val KEY_FONT_COLOR = "gui_font_color"
        const val KEY_TEXT_SCALE = "gui_text_scale"
        const val KEY_BEAT_INTERVAL = "beat_min_interval_ms"
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
        const val LEGACY_KEY_MORPH_BEATS = "morph_beats"

        /** The tempo the old beat-count morph fell back to; used only to migrate a saved value. */
        const val LEGACY_MORPH_BPM = 120f

        const val PRESET_MORPH_SECONDS_MAX = 8f
        const val KEY_REDUCED_MOTION = "gui_reduced_motion"
        const val KEY_TOUCH_SMEAR = "gui_touch_smear"
        const val KEY_TOUCH_SMEAR_STRENGTH = "gui_touch_smear_strength"
        const val KEY_TOUCH_TRANSFORM = "gui_touch_transform"
        const val KEY_KEY_COLOR = "gui_key_color"
        const val KEY_SECOND_SCREEN = "gui_second_screen"
        const val KEY_MIC_REACTIVE = "gui_mic_reactive"
        const val KEY_AUTO_ENTER_PIP = "gui_auto_enter_pip"
        const val KEY_UI_SOUNDS = "gui_ui_sounds"
    }
}
