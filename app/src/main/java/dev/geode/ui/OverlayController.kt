package dev.geode.ui

import android.app.Application
import android.graphics.Bitmap
import dev.geode.data.OverlayPrefsStore
import dev.geode.playback.MediaArtwork
import dev.geode.viz.ArtTitleLayer
import dev.geode.viz.ArtTitleOptions
import dev.geode.viz.LyricLayer
import dev.geode.viz.LyricOptions
import dev.geode.viz.OverlayComposer
import dev.geode.viz.OverlayFrame
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch

/** The cover-art/title overlay's current pixels for a surface size, or null pixels to clear it. */
internal class OverlayPixels(
    val pixels: IntArray?,
    val width: Int,
    val height: Int,
)

/**
 * Owns the cover-art/title/lyric overlay's options and composes it into the ARGB pixels
 * [dev.geode.render.bridge.NativeViz.setOverlay] expects, for the live view.
 *
 * Every mutable field here (options aside, which is a [StateFlow]) is only ever touched from
 * [storeScope], a single IO thread, so the composer's cache never sees two threads at once.
 */
internal class OverlayController(
    application: Application,
    private val prefsStore: OverlayPrefsStore,
    private val storeScope: CoroutineScope,
    private val host: Host,
) {
    interface Host {
        fun publishOverlay(pixels: OverlayPixels)
    }

    private val appContext = application.applicationContext
    private val composer = OverlayComposer()

    private val _options = MutableStateFlow(prefsStore.load())
    val options: StateFlow<ArtTitleOptions> = _options

    private val _lyricOptions = MutableStateFlow(prefsStore.loadLyric())
    val lyricOptions: StateFlow<LyricOptions> = _lyricOptions

    private var surfaceWidth = 0
    private var surfaceHeight = 0
    private var title: String? = null
    private var artist: String? = null
    private var positionMs: Long = 0L
    private var artworkUri: String? = null
    private var artwork: Bitmap? = null
    private var lyrics: Lyrics? = null
    private var lastLineIndex = NO_LINE

    init {
        composer.layers = listOf(ArtTitleLayer(_options.value), LyricLayer(_lyricOptions.value))
    }

    fun setOptions(transform: (ArtTitleOptions) -> ArtTitleOptions) {
        val updated = transform(_options.value)
        _options.value = updated
        storeScope.launch {
            artwork =
                when {
                    !updated.enabled || !updated.showArtwork -> null
                    artwork != null -> artwork
                    else -> artworkUri?.let { MediaArtwork.decodeEmbedded(appContext, it) }
                }
            composer.layers = listOf(ArtTitleLayer(updated), LyricLayer(_lyricOptions.value))
            prefsStore.save(updated)
            recompose()
        }
    }

    fun setLyricOptions(transform: (LyricOptions) -> LyricOptions) {
        val updated = transform(_lyricOptions.value)
        _lyricOptions.value = updated
        storeScope.launch {
            composer.layers = listOf(ArtTitleLayer(_options.value), LyricLayer(updated))
            prefsStore.saveLyric(updated)
            recompose()
        }
    }

    /**
     * Called whenever [PlayerSession] loads (or clears) the lyrics for the current track, so this
     * reuses that instance instead of loading its own copy.
     */
    fun setLyrics(lyrics: Lyrics?) {
        storeScope.launch {
            this@OverlayController.lyrics = lyrics
            lastLineIndex = NO_LINE
            recompose()
        }
    }

    /** Called from the GL thread's surface-size callback (forwarded through the UI layer). */
    fun onSurfaceSizeChanged(
        width: Int,
        height: Int,
    ) {
        storeScope.launch {
            if (width == surfaceWidth && height == surfaceHeight) return@launch
            surfaceWidth = width
            surfaceHeight = height
            recompose()
        }
    }

    /**
     * Called on the player's poll tick (every [dev.geode.ui.PlayerSession] `POLL_INTERVAL_MS`)
     * with the currently playing track's state.
     *
     * [ArtTitleLayer] never reads [OverlayFrame.positionMs] - it always draws once nothing else
     * queued behind it - so this only recomposes when text or artwork actually changed. The lyric
     * line is position-driven, but is throttled the same way: [lastLineIndex] only advances when
     * [Lyrics.indexAt] actually returns a different line, not on every tick's advancing position.
     */
    fun onTick(
        title: String?,
        artist: String?,
        positionMs: Long,
        artworkUri: String?,
    ) {
        storeScope.launch {
            val artworkChanged = artworkUri != this@OverlayController.artworkUri
            val changed = title != this@OverlayController.title || artist != this@OverlayController.artist || artworkChanged
            this@OverlayController.title = title
            this@OverlayController.artist = artist
            this@OverlayController.positionMs = positionMs
            if (artworkChanged) {
                this@OverlayController.artworkUri = artworkUri
                val options = _options.value
                artwork =
                    if (options.enabled && options.showArtwork) {
                        artworkUri?.let { MediaArtwork.decodeEmbedded(appContext, it) }
                    } else {
                        null
                    }
            }
            val lineIndex = lyrics?.indexAt(positionMs) ?: NO_LINE
            val lineChanged = lineIndex != lastLineIndex
            lastLineIndex = lineIndex
            if (changed || lineChanged) recompose()
        }
    }

    /**
     * Builds a per-position overlay provider for an export: the cover-art/title block is composed
     * once, since nothing in it depends on position, and the lyric line block is only recomposed
     * when [Lyrics.indexAt] returns a different line as the export's position advances -
     * [dev.geode.render.offscreen.OffscreenSceneRenderer.renderFrame] then only re-uploads the
     * overlay when the returned array is a different instance than last frame's.
     *
     * A composer of its own, not [composer]: an export runs on its own dispatcher while the live
     * view may still be running on [storeScope], and the two must never touch the same composer's
     * cache.
     */
    fun overlayProviderForExport(
        width: Int,
        height: Int,
        title: String?,
        artist: String?,
        artworkUri: String?,
    ): (Long) -> IntArray? {
        val options = _options.value
        val artLayer = ArtTitleLayer(options)
        val lyricLayer = LyricLayer(_lyricOptions.value)
        val layers = listOfNotNull(artLayer.takeIf { it.enabled }, lyricLayer.takeIf { it.enabled })
        if (layers.isEmpty()) return { null }
        // Nothing to draw: skip the artwork decode too, so an art/title-disabled export (e.g. a
        // lyrics-only one) costs nothing extra for it (the common case - the feature defaults off).
        val art =
            if (artLayer.enabled && options.showArtwork) {
                artworkUri?.let { MediaArtwork.decodeEmbedded(appContext, it) }
            } else {
                null
            }
        // Only consult lyrics (and so only vary the composed frame across the export) when the
        // lyric layer is actually enabled - otherwise this stays the fixed single-frame overlay
        // exports have always had.
        val lyricsForExport = if (lyricLayer.enabled) lyrics else null
        val exportComposer = OverlayComposer().apply { this.layers = layers }
        var composed = false
        var lastIndex = NO_LINE
        var lastPixels: IntArray? = null
        return { positionMs ->
            val index = lyricsForExport?.indexAt(positionMs) ?: NO_LINE
            if (!composed || index != lastIndex) {
                composed = true
                lastIndex = index
                val (current, next) = lineTextAt(lyricsForExport, index)
                lastPixels =
                    exportComposer.compose(
                        width,
                        height,
                        OverlayFrame(width, height, positionMs, title, artist, art, current, next),
                    )
            }
            lastPixels
        }
    }

    private fun recompose() {
        val width = surfaceWidth
        val height = surfaceHeight
        val pixels =
            if (width <= 0 || height <= 0) {
                null
            } else {
                val (current, next) = lineTextAt(lyrics, lastLineIndex)
                composer.compose(width, height, OverlayFrame(width, height, positionMs, title, artist, artwork, current, next))
            }
        host.publishOverlay(OverlayPixels(pixels, width, height))
    }

    private fun lineTextAt(
        lyrics: Lyrics?,
        index: Int,
    ): Pair<String?, String?> {
        if (lyrics == null || index < 0) return null to null
        return lyrics.lines.getOrNull(index)?.text to lyrics.lines.getOrNull(index + 1)?.text
    }

    private companion object {
        const val NO_LINE = -1
    }
}
