package dev.geode.engine.audio

import kotlin.math.roundToLong

class AudioPresentationClock(
    private val maxSegments: Int = DEFAULT_MAX_SEGMENTS,
) {
    init {
        require(maxSegments > 0) { "maxSegments must be positive, was $maxSegments" }
    }

    @Volatile
    private var snapshot: PresentationSnapshot = PresentationSnapshot(emptyList())

    val current: PresentationSnapshot get() = snapshot

    fun append(segment: ClockSegment) {
        val last = snapshot.segments.lastOrNull()
        if (last != null) {
            require(segment.presentationUsStart >= last.presentationUsStart) {
                "presentation time only moves forward; ${segment.presentationUsStart} follows ${last.presentationUsStart}"
            }
            require(segment.discontinuityGeneration >= last.discontinuityGeneration) {
                "discontinuity generation only increases"
            }
            require(segment.epoch != last.epoch || segment.inputSampleStart >= last.inputSampleStart) {
                "within an epoch input frames only move forward; " +
                    "${segment.inputSampleStart} follows ${last.inputSampleStart} in epoch ${segment.epoch}"
            }
        }
        val previous = snapshot.segments
        val dropped = maxOf(0, previous.size + 1 - maxSegments)
        val kept = ArrayList<ClockSegment>(previous.size + 1 - dropped)
        for (i in dropped until previous.size) kept.add(previous[i])
        kept.add(segment)
        snapshot = PresentationSnapshot(kept)
    }

    private companion object {
        const val DEFAULT_MAX_SEGMENTS = 64
    }
}

class PresentationSnapshot(
    val segments: List<ClockSegment>,
) {
    val epoch: Int? get() = segments.lastOrNull()?.epoch

    fun presentationTimeOf(
        inputSample: Long,
        epoch: Int,
    ): PresentationTime {
        val currentEpoch = this.epoch ?: return PresentationTime.Unknown
        if (epoch != currentEpoch) return PresentationTime.StaleEpoch(epoch, currentEpoch)

        val segment = segmentCovering(inputSample, epoch) ?: return PresentationTime.Unknown
        val offset = inputSample - segment.inputSampleStart
        if (offset < segment.skippedInputSamples) {
            return PresentationTime.Skipped(segment.inputSampleStart, segment.firstPresentedSample)
        }
        val heardOffset = offset - segment.skippedInputSamples
        return PresentationTime.At(
            segment.presentationUsStart + (heardOffset / segment.inputSamplesPerPresentationUs).roundToLong(),
        )
    }

    fun inputPositionAt(presentationUs: Long): InputPosition {
        val segment =
            segments.lastOrNull { it.presentationUsStart <= presentationUs }
                ?: return InputPosition.Unknown
        val elapsedUs = presentationUs - segment.presentationUsStart
        return InputPosition.At(
            inputSample = segment.firstPresentedSample + (elapsedUs * segment.inputSamplesPerPresentationUs).roundToLong(),
            epoch = segment.epoch,
        )
    }

    private fun segmentCovering(
        inputSample: Long,
        epoch: Int,
    ): ClockSegment? = segments.lastOrNull { it.epoch == epoch && it.inputSampleStart <= inputSample }
}
