package dev.geode.export

import androidx.media3.common.C
import androidx.media3.common.audio.SpeedProvider
import androidx.media3.common.util.UnstableApi
import dev.geode.editor.Clip
import dev.geode.editor.KeyframeTrack
import dev.geode.editor.ParamValue
import kotlin.math.abs

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
        const val MIN_SPEED = 0.25f
        const val MAX_SPEED = 4f
        private const val STEP_MS = 40L
        private const val SAME_SPEED = 1e-4f

        /** Null when the track has no keys; a track with one speed still ramps, at that speed. */
        fun fromTrack(
            track: KeyframeTrack,
            clip: Clip,
            stepMs: Long = STEP_MS,
        ): SpeedRamp? {
            if (track.isEmpty) return null
            val starts = ArrayList<Long>()
            val speeds = ArrayList<Float>()
            var sourceUs = 0L
            var outMs = 0L
            while (outMs < clip.durationMs) {
                val step = minOf(stepMs, clip.durationMs - outMs)
                val speed = speedAt(track, clip.startMs + outMs)
                if (speeds.isEmpty() || abs(speeds.last() - speed) > SAME_SPEED) {
                    starts += sourceUs
                    speeds += speed
                }
                sourceUs += (step * 1000L * speed).toLong()
                outMs += step
            }
            return SpeedRamp(starts.toLongArray(), speeds.toFloatArray(), sourceUs)
        }

        private fun speedAt(
            track: KeyframeTrack,
            timelineMs: Long,
        ): Float = ((track.valueAt(timelineMs) as? ParamValue.Scalar)?.value ?: 1f).coerceIn(MIN_SPEED, MAX_SPEED)
    }
}
