package dev.geode.widget

import android.content.Context
import android.graphics.Bitmap
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import java.io.File

/**
 * What the home-screen widget shows, written by the playback service (track, transport, progress),
 * by the app's session (the style on screen, through [WidgetStyles]) and read when the widget redraws.
 *
 * [positionMs] is a snapshot, not a clock: a widget cannot animate, so [WidgetPublisher] writes a
 * fresh one on every transport change and on its slow tick while playing, and the bar moves in
 * steps. [tick] counts those ticks; the generated visual drifts with it, which is what makes the
 * visual "periodic" without a live renderer on the home screen.
 */
data class WidgetState(
    val title: String = "",
    val artist: String = "",
    val playing: Boolean = false,
    val artPath: String? = null,
    val positionMs: Long = 0L,
    val durationMs: Long = 0L,
    val styleId: String? = null,
    val artHue: Float = NO_HUE,
    val artSpan: Float = 0f,
    val tick: Int = 0,
) {
    val hasTrack: Boolean get() = title.isNotBlank()

    /** Played share of the track, 0..1; 0 while the duration is unknown (a stream, or still preparing). */
    val progress: Float
        get() = if (durationMs > 0) (positionMs.toFloat() / durationMs).coerceIn(0f, 1f) else 0f

    companion object {
        /** [artHue] when the artwork gave no colour (none, or greyscale). */
        const val NO_HUE = -1f

        private const val PREFS = "geode-widget"
        private const val KEY_TITLE = "title"
        private const val KEY_ARTIST = "artist"
        private const val KEY_PLAYING = "playing"
        private const val KEY_ART = "art"
        private const val KEY_POSITION = "position_ms"
        private const val KEY_DURATION = "duration_ms"
        private const val KEY_STYLE = "style_id"
        private const val KEY_ART_HUE = "art_hue"
        private const val KEY_ART_SPAN = "art_span"
        private const val KEY_TICK = "tick"
        private const val ART_FILE = "widget_art.png"

        // The live copy for a widget session that is already composed. Glance keeps a session's
        // composition alive between updates, so state read once in provideGlance would go stale;
        // NowPlayingWidget collects this instead, and updateAll only has to start sessions.
        private val latest = MutableStateFlow<WidgetState?>(null)

        /** Every saved state, in this process; null until the first load or save. */
        val changes: StateFlow<WidgetState?> = latest.asStateFlow()

        fun load(context: Context): WidgetState {
            latest.value?.let { return it.withLiveArt() }
            val prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            val state =
                WidgetState(
                    title = prefs.getString(KEY_TITLE, "").orEmpty(),
                    artist = prefs.getString(KEY_ARTIST, "").orEmpty(),
                    playing = prefs.getBoolean(KEY_PLAYING, false),
                    artPath = prefs.getString(KEY_ART, null),
                    positionMs = prefs.getLong(KEY_POSITION, 0L),
                    durationMs = prefs.getLong(KEY_DURATION, 0L),
                    styleId = prefs.getString(KEY_STYLE, null),
                    artHue = prefs.getFloat(KEY_ART_HUE, NO_HUE),
                    artSpan = prefs.getFloat(KEY_ART_SPAN, 0f),
                    tick = prefs.getInt(KEY_TICK, 0),
                ).withLiveArt()
            latest.compareAndSet(null, state)
            return latest.value ?: state
        }

        fun save(
            context: Context,
            state: WidgetState,
        ) {
            latest.value = state
            context
                .getSharedPreferences(PREFS, Context.MODE_PRIVATE)
                .edit()
                .putString(KEY_TITLE, state.title)
                .putString(KEY_ARTIST, state.artist)
                .putBoolean(KEY_PLAYING, state.playing)
                .putString(KEY_ART, state.artPath)
                .putLong(KEY_POSITION, state.positionMs)
                .putLong(KEY_DURATION, state.durationMs)
                .putString(KEY_STYLE, state.styleId)
                .putFloat(KEY_ART_HUE, state.artHue)
                .putFloat(KEY_ART_SPAN, state.artSpan)
                .putInt(KEY_TICK, state.tick)
                .apply()
        }

        /**
         * Loads, changes and saves in one step. Three writers share the state (the service's
         * transport, its artwork job on IO, the session's style), so each change applies to the
         * latest copy instead of overwriting another writer's field with a stale one.
         */
        fun update(
            context: Context,
            change: (WidgetState) -> WidgetState,
        ) = synchronized(this) { save(context, change(load(context))) }

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

        // The cache directory can be cleared under us; a missing file is no artwork.
        private fun WidgetState.withLiveArt(): WidgetState = if (artPath != null && !File(artPath).isFile) copy(artPath = null) else this
    }
}
