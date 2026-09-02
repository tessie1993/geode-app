package dev.geode.export

import android.graphics.Matrix
import android.opengl.Matrix as GlMatrix
import androidx.media3.common.util.Size
import androidx.media3.common.util.UnstableApi
import androidx.media3.effect.MatrixTransformation
import androidx.media3.effect.RgbMatrix
import kotlin.math.cos
import kotlin.math.sin

/**
 * Brightness, contrast, saturation and hue as one colour matrix re-read every frame, so a clip's
 * keyframed grade actually moves. [editAt] is asked in milliseconds from the clip's first frame.
 */
@UnstableApi
class KeyframedGrade(
    private val editAt: (Long) -> ClipEdit,
) : RgbMatrix {
    private var originUs = -1L

    override fun getMatrix(
        presentationTimeUs: Long,
        useHdr: Boolean,
    ): FloatArray {
        if (originUs < 0) originUs = presentationTimeUs
        val edit = editAt((presentationTimeUs - originUs) / 1000L)
        return gradeMatrix(edit.brightness, edit.contrast, edit.saturation, edit.hueDegrees)
    }
}

/** A rotation re-read every frame, kept undistorted by rotating in square space. */
@UnstableApi
class KeyframedRotation(
    private val degreesAt: (Long) -> Float,
) : MatrixTransformation {
    private var aspect = 1f
    private var originUs = -1L

    override fun configure(
        inputWidth: Int,
        inputHeight: Int,
    ): Size {
        aspect = inputWidth.toFloat() / inputHeight.coerceAtLeast(1)
        return Size(inputWidth, inputHeight)
    }

    override fun getMatrix(presentationTimeUs: Long): Matrix {
        if (originUs < 0) originUs = presentationTimeUs
        val degrees = degreesAt((presentationTimeUs - originUs) / 1000L)
        return Matrix().apply {
            postScale(aspect, 1f)
            postRotate(degrees)
            postScale(1f / aspect, 1f)
        }
    }
}

/** Column-major 4x4: hue ∘ saturation ∘ contrast ∘ brightness, the order [ClipEdit.videoEffects] applies them. */
internal fun gradeMatrix(
    brightness: Float,
    contrast: Float,
    saturationPercent: Float,
    hueDegrees: Float,
): FloatArray {
    val result = FloatArray(16)
    val scratch = FloatArray(16)
    GlMatrix.multiplyMM(scratch, 0, contrastMatrix(contrast), 0, brightnessMatrix(brightness), 0)
    GlMatrix.multiplyMM(result, 0, saturationMatrix(1f + saturationPercent / 100f), 0, scratch, 0)
    GlMatrix.multiplyMM(scratch, 0, hueMatrix(hueDegrees), 0, result, 0)
    return scratch
}

private fun brightnessMatrix(brightness: Float): FloatArray =
    identity().also {
        it[12] = brightness
        it[13] = brightness
        it[14] = brightness
    }

// Media3's Contrast: (c - 0.5) * (1 + k) / (1.0001 - k) + 0.5.
private fun contrastMatrix(contrast: Float): FloatArray {
    val factor = (1f + contrast) / (1.0001f - contrast)
    val offset = 0.5f * (1f - factor)
    return identity().also {
        it[0] = factor
        it[5] = factor
        it[10] = factor
        it[12] = offset
        it[13] = offset
        it[14] = offset
    }
}

private fun saturationMatrix(s: Float): FloatArray {
    val r = LUMA_R * (1f - s)
    val g = LUMA_G * (1f - s)
    val b = LUMA_B * (1f - s)
    return rows(
        r + s, g, b,
        r, g + s, b,
        r, g, b + s,
    )
}

private fun hueMatrix(degrees: Float): FloatArray {
    val a = Math.toRadians(degrees.toDouble())
    val c = cos(a).toFloat()
    val s = sin(a).toFloat()
    return rows(
        0.213f + 0.787f * c - 0.213f * s, 0.715f - 0.715f * c - 0.715f * s, 0.072f - 0.072f * c + 0.928f * s,
        0.213f - 0.213f * c + 0.143f * s, 0.715f + 0.285f * c + 0.140f * s, 0.072f - 0.072f * c - 0.283f * s,
        0.213f - 0.213f * c - 0.787f * s, 0.715f - 0.715f * c + 0.715f * s, 0.072f + 0.928f * c + 0.072f * s,
    )
}

/** Builds the column-major array from a 3x3 given row by row (output R, then G, then B). */
private fun rows(
    rr: Float, rg: Float, rb: Float,
    gr: Float, gg: Float, gb: Float,
    br: Float, bg: Float, bb: Float,
): FloatArray =
    identity().also {
        it[0] = rr
        it[4] = rg
        it[8] = rb
        it[1] = gr
        it[5] = gg
        it[9] = gb
        it[2] = br
        it[6] = bg
        it[10] = bb
    }

private fun identity(): FloatArray = FloatArray(16).also { GlMatrix.setIdentityM(it, 0) }

private const val LUMA_R = 0.2126f
private const val LUMA_G = 0.7152f
private const val LUMA_B = 0.0722f
