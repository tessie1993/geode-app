package dev.geode.viz

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.net.Uri
import dev.geode.render.UnderlayBlend
import dev.geode.render.offscreen.OffscreenUnderlay
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlin.math.max

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
            val blurred = if (blurRadius > 0) downscaleUpscaleBlur(scaled, blurRadius) else scaled
            if (blurred !== scaled) scaled.recycle()
            val out = IntArray(targetW * targetH)
            blurred.getPixels(out, 0, targetW, 0, 0, targetW, targetH)
            blurred.recycle()
            if (dim > 0f) applyDim(out, dim)
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

    /** Crops [src] to [targetW]:[targetH]'s aspect ratio, centred; never upsizes. */
    private fun centerCropToAspect(
        src: Bitmap,
        targetW: Int,
        targetH: Int,
    ): Bitmap {
        val targetAspect = targetW.toFloat() / targetH
        val srcAspect = src.width.toFloat() / src.height
        return when {
            srcAspect > targetAspect -> {
                val cropW = (src.height * targetAspect).toInt().coerceIn(1, src.width)
                Bitmap.createBitmap(src, (src.width - cropW) / 2, 0, cropW, src.height)
            }
            srcAspect < targetAspect -> {
                val cropH = (src.width / targetAspect).toInt().coerceIn(1, src.height)
                Bitmap.createBitmap(src, 0, (src.height - cropH) / 2, src.width, cropH)
            }
            else -> src
        }
    }

    /**
     * Cheap blur: downscale by (1 + [radius]) then scale back up with bilinear filtering.
     *
     * A real multi-pass box blur over the full-size bitmap is the more faithful result, but there
     * is no native path to lean on here, so it costs O(pixels x passes) in plain Kotlin. Shrinking
     * first turns every output pixel's neighbourhood average into one bilinear sample the GPU-backed
     * bitmap scaler already does for free, so the same softening costs O(pixels / divisor^2) instead
     * — cheap enough to redo on every pick or slider drag without a visible stall, which matters more
     * for a background layer than an exact blur kernel shape.
     */
    private fun downscaleUpscaleBlur(
        src: Bitmap,
        radius: Int,
    ): Bitmap {
        val divisor = (1 + radius).coerceIn(1, MAX_BLUR_DIVISOR)
        if (divisor <= 1) return src
        val smallW = max(1, src.width / divisor)
        val smallH = max(1, src.height / divisor)
        val small = Bitmap.createScaledBitmap(src, smallW, smallH, true)
        val back = Bitmap.createScaledBitmap(small, src.width, src.height, true)
        small.recycle()
        return back
    }

    /** Darkens [pixels] toward black by [dim] (0 = untouched, 1 = black), alpha unchanged. */
    private fun applyDim(
        pixels: IntArray,
        dim: Float,
    ) {
        val keep = 1f - dim.coerceIn(0f, 1f)
        for (i in pixels.indices) {
            val p = pixels[i]
            val a = p ushr 24 and 0xFF
            val r = ((p ushr 16 and 0xFF) * keep).toInt()
            val g = ((p ushr 8 and 0xFF) * keep).toInt()
            val b = ((p and 0xFF) * keep).toInt()
            pixels[i] = (a shl 24) or (r shl 16) or (g shl 8) or b
        }
    }

    private const val MAX_BLUR_DIVISOR = 32
}
