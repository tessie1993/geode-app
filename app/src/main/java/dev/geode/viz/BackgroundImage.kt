package dev.geode.viz

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.net.Uri
import dev.geode.engine.bridge.GeodeNative
import dev.geode.render.UnderlayBlend
import dev.geode.render.offscreen.OffscreenUnderlay
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/** The user's background image choice, not yet decoded - an export decodes it at its own render size. */
data class BackgroundExportSpec(
    val uri: Uri,
    val blend: UnderlayBlend,
    val amount: Float,
    val blurRadius: Int,
    val dim: Float,
)

/**
 * A decoded background image, ready to upload: ARGB pixels, [width]x[height] apart.
 *
 * A plain class, not a data class: [pixels] is a large array, and the compiler-generated
 * equals()/hashCode() a data class would get compare it by reference anyway, which is misleading.
 */
class BackgroundPixels(
    val pixels: IntArray,
    val width: Int,
    val height: Int,
)

/**
 * Decodes a user-picked background image to exactly `renderWidth`x`renderHeight` ARGB pixels for
 * `NativeViz.setUnderlay` (live) or the offscreen renderer's equivalent (export, at export size).
 *
 * Every call runs on [Dispatchers.IO]; nothing here ever runs on Main.
 */
object BackgroundImage {
    /** [decode]s [spec] at [renderWidth]x[renderHeight], wrapped for [dev.geode.render.offscreen.OffscreenRenderSpec.underlay]. */
    suspend fun decodeForExport(
        context: Context,
        spec: BackgroundExportSpec,
        renderWidth: Int,
        renderHeight: Int,
    ): OffscreenUnderlay? =
        decode(context, spec.uri, renderWidth, renderHeight, spec.blurRadius, spec.dim)?.let {
            OffscreenUnderlay(it.pixels, it.width, it.height, spec.blend.ordinal, spec.amount)
        }

    suspend fun decode(
        context: Context,
        uri: Uri,
        renderWidth: Int,
        renderHeight: Int,
        blurRadius: Int,
        dim: Float,
    ): BackgroundPixels? =
        withContext(Dispatchers.IO) {
            val targetW = renderWidth.coerceAtLeast(1)
            val targetH = renderHeight.coerceAtLeast(1)
            val decoded = decodeSampled(context, uri, targetW, targetH) ?: return@withContext null
            val srcW = decoded.width
            val srcH = decoded.height
            val src = IntArray(srcW * srcH)
            decoded.getPixels(src, 0, srcW, 0, 0, srcW, srcH)
            decoded.recycle()
            val out = IntArray(targetW * targetH)
            // Crop, resize, blur and dim are native (core/viz/UnderlayPrep), the same ARGB ints in and out.
            if (GeodeNative.underlayPrepare(src, srcW, srcH, targetW, targetH, blurRadius, dim, out) == 0) {
                return@withContext null
            }
            BackgroundPixels(out, targetW, targetH)
        }

    /**
     * Decodes [uri] no larger than roughly [targetW]x[targetH] (the "render size x1" cap): reads the
     * bounds first, then lets native pick the power-of-two `inSampleSize` that still leaves the decode
     * at least the target size, so the crop has real pixels without a full-resolution decode in memory.
     */
    private fun decodeSampled(
        context: Context,
        uri: Uri,
        targetW: Int,
        targetH: Int,
    ): Bitmap? {
        val bounds =
            context.contentResolver.openInputStream(uri)?.use { stream ->
                BitmapFactory.Options().apply { inJustDecodeBounds = true }.also {
                    BitmapFactory.decodeStream(stream, null, it)
                }
            } ?: return null
        if (bounds.outWidth <= 0 || bounds.outHeight <= 0) return null
        val sample = GeodeNative.underlaySampleSize(bounds.outWidth, bounds.outHeight, targetW, targetH)
        return context.contentResolver.openInputStream(uri)?.use { stream ->
            BitmapFactory.decodeStream(stream, null, BitmapFactory.Options().apply { inSampleSize = sample })
        }
    }
}
