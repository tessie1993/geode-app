package dev.geode.data

import android.content.SharedPreferences
import dev.geode.data.prefs.PrefType
import dev.geode.data.prefs.PrefsCodec
import dev.geode.data.prefs.SettingsRepository
import dev.geode.data.prefs.prefKeys

/**
 * Settings › Library's options kept in `geode-prefs` (#1009, PR 5; was part of `GuiPrefs`): the
 * folder presets are mirrored to. The minimum track length and the music folders are
 * `geode-library`'s, still read by [LibraryPrefsStore] and [MusicLibraryRepository].
 */
data class LibrarySettings(
    val presetMirrorUri: String? = null,
)

typealias LibrarySettingsRepository = SettingsRepository<LibrarySettings>

object LibrarySettingsCodec : PrefsCodec<LibrarySettings> {
    internal const val KEY_PRESET_MIRROR_URI = "preset_mirror_uri"

    override val defaults = LibrarySettings()

    override val keys = prefKeys(GeodePrefsFiles.GENERAL) { device(PrefType.STRING, KEY_PRESET_MIRROR_URI) }

    override fun read(prefs: SharedPreferences) = LibrarySettings(prefs.getString(KEY_PRESET_MIRROR_URI, null))

    override fun write(
        editor: SharedPreferences.Editor,
        value: LibrarySettings,
    ) {
        editor.putString(KEY_PRESET_MIRROR_URI, value.presetMirrorUri)
    }
}
