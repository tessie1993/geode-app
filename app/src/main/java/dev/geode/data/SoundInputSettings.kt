package dev.geode.data

import android.content.SharedPreferences
import dev.geode.data.prefs.PrefType
import dev.geode.data.prefs.PrefsCodec
import dev.geode.data.prefs.SettingsRepository
import dev.geode.data.prefs.prefKeys
import dev.geode.engine.audio.SoundTuning

/** What the visual hears and how (#1009, PR 5; was part of `GuiPrefs`). */
data class SoundInputSettings(
    /** The microphone as a layer under the player; a restored backup turns the request on. */
    val micReactive: Boolean = false,
    /** The sound engine's tuning (Settings › Audio): every surface and the export read it. */
    val soundTuning: SoundTuning = SoundTuning(),
)

typealias SoundInputSettingsRepository = SettingsRepository<SoundInputSettings>

/** How [SoundInputSettings] sit in `geode-prefs`, under the keys `GuiPrefs` used. */
object SoundInputSettingsCodec : PrefsCodec<SoundInputSettings> {
    internal const val KEY_MIC_REACTIVE = "gui_mic_reactive"
    internal const val KEY_SOUND_TUNING = "sound_tuning"

    override val defaults = SoundInputSettings()

    override val keys =
        prefKeys(GeodePrefsFiles.GENERAL) {
            portable(PrefType.BOOLEAN, KEY_MIC_REACTIVE)
            portable(PrefType.STRING, KEY_SOUND_TUNING)
        }

    override fun read(prefs: SharedPreferences) =
        SoundInputSettings(
            micReactive = prefs.getBoolean(KEY_MIC_REACTIVE, defaults.micReactive),
            soundTuning = readSoundTuning(prefs),
        )

    override fun write(
        editor: SharedPreferences.Editor,
        value: SoundInputSettings,
    ) {
        editor.putBoolean(KEY_MIC_REACTIVE, value.micReactive)
        // The engine's defaults save as no key, so they follow native's schema when it changes.
        val tuning = value.soundTuning.savedFloats()
        if (tuning == null) editor.remove(KEY_SOUND_TUNING) else editor.putString(KEY_SOUND_TUNING, tuning.joinToString(","))
    }

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
}
