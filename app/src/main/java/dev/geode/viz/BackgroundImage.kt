package dev.geode.viz

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.net.Uri
import dev.geode.engine.bridge.GeodeNative
import dev.geode.render.UnderlayBlend
import dev.geode.render.look.LookUnderlay
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
 * Every call runs on [Dispatchers.IO]; nothing here ever runs on Main. Only the platform's part is
 * here (decode, the cut and filtered scale, reading the pixels); where to cut, the blur and the dim are
 * native (core/viz/Underlay), so one bake serves the live underlay and every export.
 */
object BackgroundImage {
    /** [decode]s [spec] at [renderWidth]x[renderHeight], wrapped for [dev.geode.render.look.LookSnapshot.underlay]. */
    suspend fun decodeForExport(
        context: Context,
        spec: BackgroundExportSpec,
        renderWidth: Int,
        renderHeight: Int,
    ): LookUnderlay? =
        decode(context, spec.uri, renderWidth, renderHeight, spec.blurRadius, spec.dim)?.let {
            LookUnderlay(it.pixels, it.width, it.height, spec.blend.ordinal, spec.amount)
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
            val cropped = centerCropToAspect(decoded, targetW, targetH)
            if (cropped !== decoded) decoded.recycle()
            val scaled =
                if (cropped.width == targetW && cropped.height == targetH) {
                    cropped
                } else {
                    Bitmap.createScaledBitmap(cropped, targetW, targetH, true).also {
                        if (it !== cropped) cropped.recycle()
                    }
                }
            val out = IntArray(targetW * targetH)
            scaled.getPixels(out, 0, targetW, 0, 0, targetW, targetH)
            scaled.recycle()
            // Blur, then dim, natively (core/viz/Underlay), the same bake for live and export.
            GeodeNative.underlayFinish(out, targetW, targetH, blurRadius, dim)
            BackgroundPixels(out, targetW, targetH)
        }

    /**
     * Decodes [uri] no larger than roughly [targetW]x[targetH] (the "render size x1" cap): reads
     * the bounds first, then picks the largest power-of-two `inSampleSize` that still leaves the
     * decoded image at least as big as the target in both dimensions, so the centre-crop below has
     * real pixels to work with without ever holding a full-resolution decode in memory.
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
        var sample = 1
        while (bounds.outWidth / (sample * 2) >= targetW && bounds.outHeight / (sample * 2) >= targetH) sample *= 2
        return context.contentResolver.openInputStream(uri)?.use { stream ->
            BitmapFactory.decodeStream(stream, null, BitmapFactory.Options().apply { inSampleSize = sample })
        }
    }

    /** Cuts [src] to [targetW]:[targetH]'s aspect at the centred rect core/viz/Underlay picks; never upsizes. */
    private fun centerCropToAspect(
        src: Bitmap,
        targetW: Int,
        targetH: Int,
    ): Bitmap {
        val r = GeodeNative.underlayCropRect(src.width, src.height, targetW, targetH)
        return if (r[2] == src.width && r[3] == src.height) src else Bitmap.createBitmap(src, r[0], r[1], r[2], r[3])
    }
}
