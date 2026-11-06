package dev.geode.export

import android.graphics.Matrix
import androidx.media3.common.util.Size
import androidx.media3.common.util.UnstableApi
import androidx.media3.effect.MatrixTransformation
import androidx.media3.effect.RgbMatrix
import dev.geode.engine.bridge.GeodeNative

/**
 * Brightness, contrast, saturation and hue as one colour matrix re-read every frame, so a clip's
 * keyframed grade actually moves. [editAt] is asked in milliseconds from the clip's first frame.
 *
 * [sourceInUs] is the clip's source-in point (its [androidx.media3.common.MediaItem.ClippingConfiguration]
 * start), in microseconds: the origin has to be this known value rather than whichever frame is
 * presented first, because a clip trimmed into its source can have the decoder present a warm-up
 * frame from before the trim point first, which would otherwise latch a wrong, earlier origin.
 */
@UnstableApi
class KeyframedGrade(
    private val sourceInUs: Long,
    private val editAt: (Long) -> ClipEdit,
) : RgbMatrix {
    override fun getMatrix(
        presentationTimeUs: Long,
        useHdr: Boolean,
    ): FloatArray {
        val edit = editAt((presentationTimeUs - sourceInUs).coerceAtLeast(0L) / 1000L)
        return gradeMatrix(edit.brightness, edit.contrast, edit.saturation, edit.hueDegrees)
    }
}

/** A rotation re-read every frame, kept undistorted by rotating in square space. See [KeyframedGrade] for [sourceInUs]. */
@UnstableApi
class KeyframedRotation(
    private val sourceInUs: Long,
    private val degreesAt: (Long) -> Float,
) : MatrixTransformation {
    private var aspect = 1f

    override fun configure(
        inputWidth: Int,
        inputHeight: Int,
    ): Size {
        aspect = inputWidth.toFloat() / inputHeight.coerceAtLeast(1)
        return Size(inputWidth, inputHeight)
    }

    override fun getMatrix(presentationTimeUs: Long): Matrix {
        val degrees = degreesAt((presentationTimeUs - sourceInUs).coerceAtLeast(0L) / 1000L)
        return Matrix().apply {
            postScale(aspect, 1f)
            postRotate(degrees)
            postScale(1f / aspect, 1f)
        }
    }
}

/**
 * Column-major 4x4: hue ∘ saturation ∘ contrast ∘ brightness, the order [ClipEdit.videoEffects] applies
 * them. The maths is native (`core/viz/ClipGrade`, tested by `core/tests/clip_media_test.cpp`); this only
 * hands over the four values.
 */
internal fun gradeMatrix(
    brightness: Float,
    contrast: Float,
    saturationPercent: Float,
    hueDegrees: Float,
): FloatArray = FloatArray(16).also { GeodeNative.clipGradeMatrix(brightness, contrast, saturationPercent, hueDegrees, it) }
