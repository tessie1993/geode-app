package dev.geode.widget

import android.content.Context
import android.graphics.Bitmap
import java.io.File

/** What the home-screen widget shows, written by the playback service and read when the widget redraws. */
data class WidgetState(
    val title: String = "",
    val artist: String = "",
    val playing: Boolean = false,
    val artPath: String? = null,
) {
    val hasTrack: Boolean get() = title.isNotBlank()

    companion object {
        private const val PREFS = "geode-widget"
        private const val KEY_TITLE = "title"
        private const val KEY_ARTIST = "artist"
        private const val KEY_PLAYING = "playing"
        private const val KEY_ART = "art"
        private const val ART_FILE = "widget_art.png"

        fun load(context: Context): WidgetState {
            val prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            return WidgetState(
                title = prefs.getString(KEY_TITLE, "").orEmpty(),
                artist = prefs.getString(KEY_ARTIST, "").orEmpty(),
                playing = prefs.getBoolean(KEY_PLAYING, false),
                artPath = prefs.getString(KEY_ART, null)?.takeIf { File(it).isFile },
            )
        }

        fun save(
            context: Context,
            state: WidgetState,
        ) {
            context
                .getSharedPreferences(PREFS, Context.MODE_PRIVATE)
                .edit()
                .putString(KEY_TITLE, state.title)
                .putString(KEY_ARTIST, state.artist)
                .putBoolean(KEY_PLAYING, state.playing)
                .putString(KEY_ART, state.artPath)
                .apply()
        }

        /** Writes the artwork the widget will decode; null clears it. Returns the path stored. */
        fun writeArt(
            context: Context,
            art: Bitmap?,
        ): String? {
            val file = File(context.cacheDir, ART_FILE)
            if (art == null) {
                file.delete()
                return null
            }
            val written = runCatching { file.outputStream().use { art.compress(Bitmap.CompressFormat.PNG, 100, it) } }.isSuccess
            return if (written) file.absolutePath else null
        }
    }
}
