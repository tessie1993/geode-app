package dev.geode.export

import android.content.ContentValues
import android.content.Context
import android.graphics.Bitmap
import android.graphics.Matrix
import android.net.Uri
import android.os.Build
import android.provider.MediaStore
import dev.geode.analysis.FeatureTimeline
import dev.geode.render.AdsrConfig
import dev.geode.render.LfoConfig
import dev.geode.render.SceneFactory
import dev.geode.render.offscreen.OffscreenRenderSpec
import dev.geode.render.offscreen.OffscreenSceneRenderer
import dev.geode.render.scene.SceneParams
import dev.geode.util.bestEffort
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.OutputStream

/**
 * Renders a single still frame of the current scene — the "grab a frame" export alongside video
 * — through the same [OffscreenSceneRenderer] a video export drives, but reads the frame back
 * with [PixelReadback] instead of handing it to an encoder.
 */
class StillExporter(
    private val context: Context,
) {
    sealed interface Result {
        data class Saved(
            val uri: Uri,
        ) : Result

        data class Failed(
            val message: String,
        ) : Result
    }

    /**
     * Renders [sceneParams] through [sceneFactory] at [positionMs] (measured from the start of
     * the track [timeline] analysed), using the features [timeline] reports for that instant, and
     * saves the result as a PNG at [aspect]'s resolution.
     *
     * With [destination] null the file lands in `Pictures/Geode` via `MediaStore.Images` (API 29+
     * scoped storage); a non-null [destination] — the SAF picker's result on older API levels —
     * is written to directly.
     */
    @Suppress("TooGenericExceptionCaught")
    suspend fun export(
        timeline: FeatureTimeline,
        sceneFactory: SceneFactory,
        aspect: ExportAspect,
        sceneParams: SceneParams,
        positionMs: Long,
        fileName: String,
        lfoConfigs: List<LfoConfig> = emptyList(),
        adsrConfigs: List<AdsrConfig> = emptyList(),
        reducedMotion: Boolean = false,
        destination: Uri? = null,
    ): Result =
        withContext(Dispatchers.Default) {
            val bitmap =
                try {
                    renderBitmap(timeline, sceneFactory, aspect, sceneParams, positionMs, lfoConfigs, adsrConfigs, reducedMotion)
                } catch (e: Exception) {
                    return@withContext Result.Failed(e.message ?: e.javaClass.simpleName)
                }
            try {
                if (destination != null) saveToDestination(bitmap, destination) else saveToMediaStore(bitmap, fileName)
            } finally {
                bitmap.recycle()
            }
        }

    /** Must run on the calling thread: it makes an EGL context current there and leaves it that way until [PixelReadback.release]. */
    private fun renderBitmap(
        timeline: FeatureTimeline,
        sceneFactory: SceneFactory,
        aspect: ExportAspect,
        sceneParams: SceneParams,
        positionMs: Long,
        lfoConfigs: List<LfoConfig>,
        adsrConfigs: List<AdsrConfig>,
        reducedMotion: Boolean,
    ): Bitmap {
        var eglRef: PixelReadback? = null
        var rendererRef: OffscreenSceneRenderer? = null
        try {
            val egl = PixelReadback(aspect.width, aspect.height).also { eglRef = it }
            egl.makeCurrent()
            val renderer =
                OffscreenSceneRenderer(
                    context = context,
                    sceneFactory = sceneFactory,
                    timeline = timeline,
                    spec =
                        OffscreenRenderSpec(
                            width = aspect.width,
                            height = aspect.height,
                            fps = STILL_FPS,
                            totalFrames = 1,
                            rangeStartMs = positionMs,
                            baseParams = sceneParams,
                            lfoConfigs = lfoConfigs,
                            adsrConfigs = adsrConfigs,
                            reducedMotion = reducedMotion,
                        ),
                ).also { rendererRef = it }
            renderer.prepare()
            renderer.renderFrame(0)
            val pixels = egl.readPixels(aspect.width, aspect.height)
            val raw = Bitmap.createBitmap(aspect.width, aspect.height, Bitmap.Config.ARGB_8888)
            raw.copyPixelsFromBuffer(pixels)
            return flipVertically(raw)
        } finally {
            bestEffort(TAG, "rendererRef?.release()") { rendererRef?.release() }
            bestEffort(TAG, "eglRef?.release()") { eglRef?.release() }
        }
    }

    /** GL's row order is bottom-first; a still needs top-first to look right as a PNG. */
    private fun flipVertically(source: Bitmap): Bitmap {
        val matrix = Matrix().apply { preScale(1f, -1f) }
        val flipped = Bitmap.createBitmap(source, 0, 0, source.width, source.height, matrix, false)
        if (flipped !== source) source.recycle()
        return flipped
    }

    @Suppress("TooGenericExceptionCaught")
    private fun saveToMediaStore(
        bitmap: Bitmap,
        fileName: String,
    ): Result {
        val resolver = context.contentResolver
        val values =
            ContentValues().apply {
                put(MediaStore.Images.Media.DISPLAY_NAME, fileName)
                put(MediaStore.Images.Media.MIME_TYPE, "image/png")
                if (Build.VERSION.SDK_INT >= 29) {
                    put(MediaStore.Images.Media.RELATIVE_PATH, "Pictures/Geode")
                    put(MediaStore.Images.Media.IS_PENDING, 1)
                }
            }
        val outUri =
            resolver.insert(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, values)
                ?: return Result.Failed(
                    "Your Pictures library would not accept a new file. Check that storage is not full.",
                )
        return try {
            val opened = resolver.openOutputStream(outUri)?.use { out -> writePng(bitmap, out) }
            if (opened == null) {
                bestEffort(TAG, "resolver.delete(outUri, null, null)") { resolver.delete(outUri, null, null) }
                return Result.Failed("The new file in your Pictures library could not be opened for writing.")
            }
            if (Build.VERSION.SDK_INT >= 29) {
                val done = ContentValues().apply { put(MediaStore.Images.Media.IS_PENDING, 0) }
                resolver.update(outUri, done, null, null)
            }
            Result.Saved(outUri)
        } catch (e: Exception) {
            bestEffort(TAG, "resolver.delete(outUri, null, null)") { resolver.delete(outUri, null, null) }
            Result.Failed(e.message ?: e.javaClass.simpleName)
        }
    }

    @Suppress("TooGenericExceptionCaught")
    private fun saveToDestination(
        bitmap: Bitmap,
        destination: Uri,
    ): Result =
        try {
            val opened = context.contentResolver.openOutputStream(destination)?.use { out -> writePng(bitmap, out) }
            if (opened == null) {
                Result.Failed(
                    "The folder you chose would not let the file be written. Some cloud providers refuse " +
                        "this; try your Pictures library or a folder on the device.",
                )
            } else {
                Result.Saved(destination)
            }
        } catch (e: Exception) {
            Result.Failed(e.message ?: e.javaClass.simpleName)
        }

    private fun writePng(
        bitmap: Bitmap,
        out: OutputStream,
    ) {
        check(bitmap.compress(Bitmap.CompressFormat.PNG, 100, out)) { "PNG encoding failed" }
    }

    private companion object {
        // Only frame 0 is ever rendered; the fps just sets how far spec.rangeStartMs's window of
        // features spans, matching one frame at export frame rate rather than the whole second.
        const val STILL_FPS = 60
    }
}

private const val TAG = "StillExporter"
