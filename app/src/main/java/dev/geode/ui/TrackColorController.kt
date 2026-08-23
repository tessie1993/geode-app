package dev.geode.ui

import android.app.Application
import android.graphics.BitmapFactory
import android.media.MediaMetadataRetriever
import android.net.Uri
import dev.geode.analysis.ArtPalette
import dev.geode.analysis.KeyPalette
import dev.geode.data.PaletteStore
import dev.geode.render.scene.SceneParams
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlin.math.roundToInt

internal class TrackColorController(
    private val application: Application,
    private val scope: CoroutineScope,
    private val host: Host,
) {
    interface Host {
        val currentUri: Uri?
        val keyColorEnabled: Boolean
        val params: SceneParams
        val currentTrackKey: String?

        fun setSceneParams(params: SceneParams)

        fun persistKeyColorPref(enabled: Boolean)
    }

    private var hueBeforeKeyColor: Float? = null

    private val _artPaletteNote = MutableStateFlow<String?>(null)
    val artPaletteNote: StateFlow<String?> = _artPaletteNote

    fun applyKeyColor(key: String) {
        if (!host.keyColorEnabled) return
        val hue = KeyPalette.hueFor(key) ?: return
        val params = host.params
        if (hueBeforeKeyColor == null) hueBeforeKeyColor = params.colorShift
        if (params.colorShift != hue) host.setSceneParams(params.copy(colorShift = hue))
    }

    fun setKeyColor(enabled: Boolean) {
        host.persistKeyColorPref(enabled)
        if (enabled) {
            host.currentTrackKey?.let { applyKeyColor(it) }
        } else {
            hueBeforeKeyColor?.let { host.setSceneParams(host.params.copy(colorShift = it)) }
            hueBeforeKeyColor = null
        }
    }

    fun applyArtworkPalette() {
        val uri =
            host.currentUri ?: run {
                _artPaletteNote.value = "Nothing is playing."
                return
            }
        scope.launch(Dispatchers.IO) {
            val pixels = artworkPixels(uri)
            val extracted = pixels?.let { ArtPalette.extract(it) }
            withContext(Dispatchers.Main) {
                if (host.currentUri != uri) return@withContext
                when {
                    pixels == null -> _artPaletteNote.value = "This track has no embedded artwork."
                    extracted == null ->
                        _artPaletteNote.value =
                            "The artwork has no colour to take — it is greyscale or nearly black."
                    else -> {
                        host.setSceneParams(
                            PaletteStore.applyGradient(host.params, extracted.baseHue, extracted.span),
                        )
                        _artPaletteNote.value =
                            "Palette taken from the artwork (${(extracted.confidence * 100).roundToInt()}% of it had colour)."
                    }
                }
            }
        }
    }

    private fun artworkPixels(uri: Uri): IntArray? =
        runCatching {
            val retriever = MediaMetadataRetriever()
            val bytes =
                try {
                    retriever.setDataSource(application, uri)
                    retriever.embeddedPicture
                } finally {
                    retriever.release()
                } ?: return null
            val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
            BitmapFactory.decodeByteArray(bytes, 0, bytes.size, bounds)
            val sample =
                generateSequence(1) { it * 2 }
                    .first { bounds.outWidth / it <= ART_SAMPLE_SIZE && bounds.outHeight / it <= ART_SAMPLE_SIZE }
            val opts = BitmapFactory.Options().apply { inSampleSize = sample }
            val bmp = BitmapFactory.decodeByteArray(bytes, 0, bytes.size, opts) ?: return null
            val out = IntArray(bmp.width * bmp.height)
            bmp.getPixels(out, 0, bmp.width, 0, 0, bmp.width, bmp.height)
            bmp.recycle()
            out
        }.getOrNull()

    private companion object {
        const val ART_SAMPLE_SIZE = 128
    }
}
