package dev.geode.ui

import android.app.Application
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.net.Uri
import androidx.core.net.toUri
import dev.geode.RingLog
import dev.geode.data.OverlayPrefsStore
import dev.geode.playback.MediaArtwork
import dev.geode.viz.ArtTitleLayer
import dev.geode.viz.ArtTitleOptions
import dev.geode.viz.LyricLayer
import dev.geode.viz.LyricOptions
import dev.geode.viz.OverlayComposer
import dev.geode.viz.OverlayFrame
import dev.geode.viz.WatermarkLayer
import dev.geode.viz.WatermarkOptions
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
 * Owns the cover-art/title/lyric/watermark overlay's options and composes it into the ARGB pixels
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

    private val _watermarkOptions = MutableStateFlow(prefsStore.loadWatermark())
    val watermarkOptions: StateFlow<WatermarkOptions> = _watermarkOptions

    private val _lyricOptions = MutableStateFlow(prefsStore.loadLyric())
    val lyricOptions: StateFlow<LyricOptions> = _lyricOptions

    private var surfaceWidth = 0
    private var surfaceHeight = 0
    private var title: String? = null
    private var artist: String? = null
    private var positionMs: Long = 0L
    private var artworkUri: String? = null
    private var artwork: Bitmap? = null

    // Read from both storeScope (live view) and the export dispatcher (overlayProviderForExport).
    // The reference is only ever replaced, never recycled while still assigned here - an export
    // may be mid-draw with the old bitmap on its own thread - so @Volatile alone is enough for
    // safe publication; the stale bitmap is left for the collector instead.
    @Volatile
    private var watermarkBitmap: Bitmap? = null

    private var lyrics: Lyrics? = null
    private var lastLineIndex = NO_LINE

    init {
        composer.layers =
            listOf(ArtTitleLayer(_options.value), WatermarkLayer(_watermarkOptions.value, null), LyricLayer(_lyricOptions.value))
        storeScope.launch {
            val persisted = _watermarkOptions.value
            val uri = persisted.uri
            if (uri == null) return@launch
            val bitmap = decodeWatermarkBitmap(uri.toUri())
            if (bitmap == null) {
                RingLog.note("Watermark", "persisted watermark image unreadable, clearing")
                val cleared = persisted.copy(enabled = false, uri = null)
                _watermarkOptions.value = cleared
                prefsStore.saveWatermark(cleared)
                composer.layers = listOf(ArtTitleLayer(_options.value), WatermarkLayer(cleared, null), LyricLayer(_lyricOptions.value))
            } else {
                watermarkBitmap = bitmap
                composer.layers = listOf(ArtTitleLayer(_options.value), WatermarkLayer(persisted, bitmap), LyricLayer(_lyricOptions.value))
                recompose()
            }
        }
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
            composer.layers =
                listOf(ArtTitleLayer(updated), WatermarkLayer(_watermarkOptions.value, watermarkBitmap), LyricLayer(_lyricOptions.value))
            prefsStore.save(updated)
            recompose()
        }
    }

    /** Changes corner/size/opacity without touching the picked image. */
    fun setWatermarkOptions(transform: (WatermarkOptions) -> WatermarkOptions) {
        val updated = transform(_watermarkOptions.value)
        _watermarkOptions.value = updated
        storeScope.launch {
            composer.layers =
                listOf(ArtTitleLayer(_options.value), WatermarkLayer(updated, watermarkBitmap), LyricLayer(_lyricOptions.value))
            prefsStore.saveWatermark(updated)
            recompose()
        }
    }

    fun setLyricOptions(transform: (LyricOptions) -> LyricOptions) {
        val updated = transform(_lyricOptions.value)
        _lyricOptions.value = updated
        storeScope.launch {
            composer.layers =
                listOf(ArtTitleLayer(_options.value), WatermarkLayer(_watermarkOptions.value, watermarkBitmap), LyricLayer(updated))
            prefsStore.saveLyric(updated)
            recompose()
        }
    }

    /**
     * Called once the [androidx.activity.result.contract.ActivityResultContracts.OpenDocument]
     * picker has returned a uri and the caller has already taken a persistable read permission on
     * it. Decodes it once, downsampled, and caches the result for both the live view and exports.
     */
    fun pickWatermarkImage(uri: Uri) {
        storeScope.launch {
            val bitmap = decodeWatermarkBitmap(uri)
            if (bitmap == null) {
                RingLog.note("Watermark", "picked watermark image could not be decoded")
                return@launch
            }
            watermarkBitmap = bitmap
            val updated = _watermarkOptions.value.copy(enabled = true, uri = uri.toString())
            _watermarkOptions.value = updated
            prefsStore.saveWatermark(updated)
            composer.layers = listOf(ArtTitleLayer(_options.value), WatermarkLayer(updated, bitmap), LyricLayer(_lyricOptions.value))
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

    /** Drops the picked image and releases its cached bitmap reference. */
    fun clearWatermarkImage() {
        storeScope.launch {
            watermarkBitmap = null
            val updated = _watermarkOptions.value.copy(enabled = false, uri = null)
            _watermarkOptions.value = updated
            prefsStore.saveWatermark(updated)
            composer.layers = listOf(ArtTitleLayer(_options.value), WatermarkLayer(updated, null), LyricLayer(_lyricOptions.value))
            recompose()
        }
    }

    /**
     * Decodes [uri] downsampled to at most [WATERMARK_MAX_PX] on its long edge, the same
     * two-pass `inSampleSize` approach as [MediaArtwork.decodeBytes] but reading straight from
     * the content uri instead of a byte array already in memory.
     */
    private fun decodeWatermarkBitmap(uri: Uri): Bitmap? =
        runCatching {
            val resolver = appContext.contentResolver
            val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
            resolver.openInputStream(uri)?.use { BitmapFactory.decodeStream(it, null, bounds) }
            val longest = maxOf(bounds.outWidth, bounds.outHeight)
            if (longest <= 0) return@runCatching null
            var sample = 1
            while ((longest / (sample * 2)) >= WATERMARK_MAX_PX) sample *= 2
            val opts = BitmapFactory.Options().apply { inSampleSize = sample }
            resolver.openInputStream(uri)?.use { BitmapFactory.decodeStream(it, null, opts) }
        }.getOrNull()

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
     * Builds a per-position overlay provider for an export: the cover-art/title and watermark
     * blocks are composed once, since neither depends on position, and the lyric line block is
     * only recomposed when [Lyrics.indexAt] returns a different line as the export's position
     * advances - [dev.geode.render.offscreen.OffscreenSceneRenderer.renderFrame] then only
     * re-uploads the overlay when the returned array is a different instance than last frame's.
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
        val watermarkLayer = WatermarkLayer(_watermarkOptions.value, watermarkBitmap)
        val lyricLayer = LyricLayer(_lyricOptions.value)
        val layers =
            listOfNotNull(
                artLayer.takeIf { it.enabled },
                watermarkLayer.takeIf { it.enabled },
                lyricLayer.takeIf { it.enabled },
            )
        if (layers.isEmpty()) return { null }
        // Nothing to draw: skip the artwork decode too, so an art/title-disabled export (e.g. a
        // lyrics-only or watermark-only one) costs nothing extra for it (the common case - the
        // feature defaults off).
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
        /** Long-edge cap for a decoded watermark - plenty for a logo drawn at up to 30% of frame. */
        const val WATERMARK_MAX_PX = 512
        const val NO_LINE = -1
    }
}
