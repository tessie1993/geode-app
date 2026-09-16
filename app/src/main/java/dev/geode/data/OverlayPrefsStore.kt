package dev.geode.data

import android.content.SharedPreferences
import dev.geode.viz.ArtTitleOptions
import dev.geode.viz.LyricOptions
import dev.geode.viz.LyricPosition
import dev.geode.viz.LyricSize
import dev.geode.viz.OverlayPosition
import dev.geode.viz.OverlaySize

class OverlayPrefsStore(
    private val prefs: SharedPreferences,
) {
    fun load(): ArtTitleOptions {
        val d = ArtTitleOptions()
        return ArtTitleOptions(
            enabled = prefs.getBoolean(KEY_ENABLED, d.enabled),
            position =
                runCatching { OverlayPosition.valueOf(prefs.getString(KEY_POSITION, null) ?: d.position.name) }
                    .getOrDefault(d.position),
            size =
                runCatching { OverlaySize.valueOf(prefs.getString(KEY_SIZE, null) ?: d.size.name) }
                    .getOrDefault(d.size),
            showArtwork = prefs.getBoolean(KEY_SHOW_ARTWORK, d.showArtwork),
            showText = prefs.getBoolean(KEY_SHOW_TEXT, d.showText),
        )
    }

    fun save(options: ArtTitleOptions) {
        prefs
            .edit()
            .putBoolean(KEY_ENABLED, options.enabled)
            .putString(KEY_POSITION, options.position.name)
            .putString(KEY_SIZE, options.size.name)
            .putBoolean(KEY_SHOW_ARTWORK, options.showArtwork)
            .putBoolean(KEY_SHOW_TEXT, options.showText)
            .apply()
    }

    fun loadLyric(): LyricOptions {
        val d = LyricOptions()
        return LyricOptions(
            enabled = prefs.getBoolean(KEY_LYRIC_ENABLED, d.enabled),
            position =
                runCatching { LyricPosition.valueOf(prefs.getString(KEY_LYRIC_POSITION, null) ?: d.position.name) }
                    .getOrDefault(d.position),
            size =
                runCatching { LyricSize.valueOf(prefs.getString(KEY_LYRIC_SIZE, null) ?: d.size.name) }
                    .getOrDefault(d.size),
        )
    }

    fun saveLyric(options: LyricOptions) {
        prefs
            .edit()
            .putBoolean(KEY_LYRIC_ENABLED, options.enabled)
            .putString(KEY_LYRIC_POSITION, options.position.name)
            .putString(KEY_LYRIC_SIZE, options.size.name)
            .apply()
    }

    private companion object {
        const val KEY_ENABLED = "overlay_arttitle_enabled"
        const val KEY_POSITION = "overlay_arttitle_position"
        const val KEY_SIZE = "overlay_arttitle_size"
        const val KEY_SHOW_ARTWORK = "overlay_arttitle_show_artwork"
        const val KEY_SHOW_TEXT = "overlay_arttitle_show_text"
        const val KEY_LYRIC_ENABLED = "overlay_lyric_enabled"
        const val KEY_LYRIC_POSITION = "overlay_lyric_position"
        const val KEY_LYRIC_SIZE = "overlay_lyric_size"
    }
}
