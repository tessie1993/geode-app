package dev.geode.export

import androidx.media3.common.C
import androidx.media3.common.audio.SpeedProvider
import androidx.media3.common.util.UnstableApi
import dev.geode.editor.Clip
import dev.geode.editor.Interpolation
import dev.geode.editor.KeyframeTrack
import dev.geode.editor.ParamValue
import dev.geode.engine.bridge.GeodeNative

/**
 * A keyframed speed track turned into the piecewise-constant speeds Media3 asks for.
 *
 * Keys are placed in output (timeline) time; the provider is asked in input (source) time, so the
 * track is sampled along the clip and each step's source consumption is integrated to find where
 * that step starts in the source. Steps with the same speed are merged into one segment.
 */
@UnstableApi
class SpeedRamp private constructor(
    private val startsUs: LongArray,
    private val speeds: FloatArray,
    val sourceSpanUs: Long,
) : SpeedProvider {
    override fun getSpeed(timeUs: Long): Float = speeds[segmentAt(timeUs)]

    override fun getNextSpeedChangeTimeUs(timeUs: Long): Long {
        val next = segmentAt(timeUs) + 1
        return if (next < startsUs.size) startsUs[next] else C.TIME_UNSET
    }

    private fun segmentAt(timeUs: Long): Int {
        var low = 0
        var high = startsUs.size - 1
        var found = 0
        while (low <= high) {
            val mid = (low + high) ushr 1
            if (startsUs[mid] <= timeUs) {
                found = mid
                low = mid + 1
            } else {
                high = mid - 1
            }
        }
        return found
    }

    companion object {
        private const val STEP_MS = 40L
        private const val VALUE_FLOATS = 4

        /**
         * Null when the track has no keys; a track with one speed still ramps, at that speed.
         *
         * The sampling, integration, merging and the 0.25..4 clamp are native
         * (`core/signal/timeline/SpeedRamp`, tested by `core/tests/timeline_test.cpp`): the keys go over once,
         * the segments come back, and only the lookup Media3 asks for per buffer stays here.
         */
        fun fromTrack(
            track: KeyframeTrack,
            clip: Clip,
            stepMs: Long = STEP_MS,
        ): SpeedRamp? {
            if (track.isEmpty || clip.durationMs <= 0L) return null
            val keys = track.keys
            val values = FloatArray(keys.size * VALUE_FLOATS)
            val curves = FloatArray(keys.size * VALUE_FLOATS)
            val interps = IntArray(keys.size)
            keys.forEachIndexed { i, key ->
                key.value.writeTo(values, i * VALUE_FLOATS)
                interps[i] = key.interpolation.writeTo(curves, i * VALUE_FLOATS)
            }
            // At most one segment per step: the native ramp merges runs of the same speed.
            val speeds = FloatArray(((clip.durationMs + stepMs - 1) / stepMs).toInt().coerceAtLeast(1))
            val out =
                GeodeNative.speedRampBuild(
                    keyTimesMs = LongArray(keys.size) { keys[it].atMs },
                    keyKinds = IntArray(keys.size) { keys[it].value.nativeKind() },
                    keyValues = values,
                    keyInterps = interps,
                    keyCurves = curves,
                    clipStartMs = clip.startMs,
                    clipDurationMs = clip.durationMs,
                    stepMs = stepMs,
                    speedsOut = speeds,
                ) ?: return null
            val segments = out.size - 1
            if (segments < 1) return null
            return SpeedRamp(out.copyOfRange(1, out.size), speeds.copyOf(segments), sourceSpanUs = out[0])
        }
    }
}

/** The kind ordinal `geode_track_add_key` takes (core/signal/timeline/Keyframes `ValueKind`). */
internal fun ParamValue.nativeKind(): Int =
    when (this) {
        is ParamValue.Scalar -> 0
        is ParamValue.Vector2 -> 1
        is ParamValue.Colour -> 2
        is ParamValue.Toggle -> 3
        is ParamValue.Choice -> 4
    }

/** This value's four floats at [offset] in [out], in the order native blends them. */
internal fun ParamValue.writeTo(
    out: FloatArray,
    offset: Int,
) {
    when (this) {
        is ParamValue.Scalar -> out[offset] = value
        is ParamValue.Vector2 -> {
            out[offset] = x
            out[offset + 1] = y
        }
        is ParamValue.Colour -> {
            out[offset] = r
            out[offset + 1] = g
            out[offset + 2] = b
            out[offset + 3] = a
        }
        is ParamValue.Toggle -> out[offset] = if (on) 1f else 0f
        is ParamValue.Choice -> out[offset] = index.toFloat()
    }
}

/** Writes this interpolation's bezier handles at [offset] in [curves]; returns the native interp ordinal. */
internal fun Interpolation.writeTo(
    curves: FloatArray,
    offset: Int,
): Int {
    val bezier =
        when (this) {
            Interpolation.Hold -> return 0
            Interpolation.Linear -> return 1
            is Interpolation.Ease -> shape.curve
            is Interpolation.Custom -> this.curve
        }
    curves[offset] = bezier.c1x
    curves[offset + 1] = bezier.c1y
    curves[offset + 2] = bezier.c2x
    curves[offset + 3] = bezier.c2y
    return 2
}
