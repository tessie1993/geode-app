package dev.geode.ui

import android.app.Application
import android.graphics.Bitmap
import dev.geode.data.OverlayPrefsStore
import dev.geode.playback.MediaArtwork
import dev.geode.viz.ArtTitleLayer
import dev.geode.viz.ArtTitleOptions
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
 * Owns the cover-art/title overlay's options and composes it into the ARGB pixels
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

    private var surfaceWidth = 0
    private var surfaceHeight = 0
    private var title: String? = null
    private var artist: String? = null
    private var positionMs: Long = 0L
    private var artworkUri: String? = null
    private var artwork: Bitmap? = null

    init {
        composer.layers = listOf(ArtTitleLayer(_options.value))
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
            composer.layers = listOf(ArtTitleLayer(updated))
            prefsStore.save(updated)
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
     * queued behind it - so this only recomposes when text or artwork actually changed, not on
     * every tick's advancing position. A future position-driven layer would need to ask for a
     * redraw itself rather than rely on this ticking it every 500ms regardless.
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
            if (changed) recompose()
        }
    }

    /**
     * Composes the overlay at an export's frame size from the exported track's own state.
     *
     * Uses a composer of its own rather than [composer]: an export runs on its own dispatcher
     * while the live view may still be running on [storeScope], and the two must never touch the
     * same composer's cache.
     */
    fun composeForExport(
        width: Int,
        height: Int,
        title: String?,
        artist: String?,
        artworkUri: String?,
    ): OverlayPixels {
        val options = _options.value
        val layer = ArtTitleLayer(options)
        // Nothing to draw: skip the artwork decode too, so a disabled overlay costs an export
        // nothing (the common case - the feature defaults to off).
        if (!layer.enabled) return OverlayPixels(null, width, height)
        val art = if (options.showArtwork) artworkUri?.let { MediaArtwork.decodeEmbedded(appContext, it) } else null
        val exportComposer = OverlayComposer().apply { layers = listOf(layer) }
        val pixels = exportComposer.compose(width, height, OverlayFrame(width, height, 0L, title, artist, art))
        return OverlayPixels(pixels, width, height)
    }

    private fun recompose() {
        val width = surfaceWidth
        val height = surfaceHeight
        val pixels =
            if (width <= 0 || height <= 0) {
                null
            } else {
                composer.compose(width, height, OverlayFrame(width, height, positionMs, title, artist, artwork))
            }
        host.publishOverlay(OverlayPixels(pixels, width, height))
    }
}
