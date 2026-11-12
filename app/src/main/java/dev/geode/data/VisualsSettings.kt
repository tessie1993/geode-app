package dev.geode.data

import android.content.SharedPreferences
import dev.geode.data.prefs.PrefType
import dev.geode.data.prefs.PrefsCodec
import dev.geode.data.prefs.SettingsRepository
import dev.geode.data.prefs.prefKeys

/**
 * The visual's own options that sat in `GuiPrefs` (#1009, PR 5): the blend between looks, the
 * second screen, and touch. Behaviour's gesture map and the style timer join it with the regroup.
 */
data class VisualsSettings(
    /**
     * How long a preset load takes to glide in, in SECONDS.
     *
     * A duration, never a count of beats: nothing here consults or assumes a tempo.
     */
    val presetMorphSeconds: Float = 2f,
    val secondScreen: Boolean = true,
    /** Settings › Gestures & controls › Touch: a pinch moves Zoom and a twist Rotation (core/viz/TouchTransform). */
    val touchTransform: Boolean = true,
)

typealias VisualsSettingsRepository = SettingsRepository<VisualsSettings>

/** How [VisualsSettings] sit in `geode-prefs`, under the keys `GuiPrefs` used. */
object VisualsSettingsCodec : PrefsCodec<VisualsSettings> {
    internal const val KEY_PRESET_MORPH_SEC = "gui_preset_morph_seconds"
    internal const val KEY_SECOND_SCREEN = "gui_second_screen"
    internal const val KEY_TOUCH_TRANSFORM = "gui_touch_transform"

    /** Retired with Touch › Smear (#1036); removed on write. */
    internal const val KEY_TOUCH_SMEAR = "gui_touch_smear"
    internal const val KEY_TOUCH_SMEAR_STRENGTH = "gui_touch_smear_strength"

    const val PRESET_MORPH_SECONDS_MAX = 8f

    override val defaults = VisualsSettings()

    override val keys =
        prefKeys(GeodePrefsFiles.GENERAL) {
            portable(PrefType.FLOAT, KEY_PRESET_MORPH_SEC)
            portable(PrefType.BOOLEAN, KEY_SECOND_SCREEN, KEY_TOUCH_TRANSFORM)
            // The morph length in beats: no longer read (the tempo is not a setting), still in old installs' files.
            retired(PrefType.INT, "morph_beats")
            retired(null, KEY_TOUCH_SMEAR, KEY_TOUCH_SMEAR_STRENGTH)
        }

    override fun read(prefs: SharedPreferences): VisualsSettings {
        val d = defaults
        return VisualsSettings(
            presetMorphSeconds = prefs.getFloat(KEY_PRESET_MORPH_SEC, d.presetMorphSeconds),
            secondScreen = prefs.getBoolean(KEY_SECOND_SCREEN, d.secondScreen),
            touchTransform = prefs.getBoolean(KEY_TOUCH_TRANSFORM, d.touchTransform),
        )
    }

    override fun normalize(value: VisualsSettings) =
        value.copy(presetMorphSeconds = value.presetMorphSeconds.coerceIn(0f, PRESET_MORPH_SECONDS_MAX))

    override fun write(
        editor: SharedPreferences.Editor,
        value: VisualsSettings,
    ) {
        editor
            .putFloat(KEY_PRESET_MORPH_SEC, value.presetMorphSeconds)
            .putBoolean(KEY_SECOND_SCREEN, value.secondScreen)
            .putBoolean(KEY_TOUCH_TRANSFORM, value.touchTransform)
            .remove(KEY_TOUCH_SMEAR)
            .remove(KEY_TOUCH_SMEAR_STRENGTH)
    }
}
