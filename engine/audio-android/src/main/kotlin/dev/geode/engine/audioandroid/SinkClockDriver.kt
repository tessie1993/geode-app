package dev.geode.engine.audioandroid

import dev.geode.engine.audio.AudioPresentationClock
import dev.geode.engine.audio.ClockSegment
import kotlin.math.roundToLong

class SinkClockDriver(
    private val clock: AudioPresentationClock,
) : SinkClockHooks,
    TapBoundaryListener {
    @Volatile
    private var skipped: SkippedFrameSource = SkippedFrameSource { 0L }

    private var speed: Float = 1f
    private var sawSpeedHook = false
    private var sawSkipHook = false
    private var speedAuthoritative = false

    private var anchorUs: Long = 0L

    private var openSlope: Double = 0.0

    private var anchorTrusted = true

    private var segmentsAppended = 0L

    override fun attachSkippedFrames(source: SkippedFrameSource) {
        skipped = source
    }

    override fun onSpeedApplied(speed: Float) {
        this.speed = speed
        sawSpeedHook = true
    }

    override fun onSkipSilenceApplied(enabled: Boolean) {
        sawSkipHook = true
    }

    override fun onTapBoundary(
        ended: PcmTapFormat?,
        endedFrames: Long,
        begun: PcmTapFormat,
    ) {
        val rawSkip = if (ended != null && endedFrames > 0L) skipped.skippedInputFramesSinceFlush() else 0L
        val trustworthy = rawSkip in 0L..endedFrames
        val skip = if (trustworthy) rawSkip else 0L

        if (openSlope > 0.0) {
            anchorUs += ((endedFrames - skip) / openSlope).roundToLong()
        } else if (ended != null && endedFrames > 0L) {
            distrustAnchor()
        }

        if (sawSkipHook) speedAuthoritative = sawSpeedHook
        sawSpeedHook = false
        sawSkipHook = false

        openSlope = 0.0
        if (!speedAuthoritative) {
            distrustAnchor()
            return
        }
        if (begun.sampleRateHz <= 0 || begun.channelCount <= 0 || begun.sampleWidth == null || !(speed > 0f)) {
            distrustAnchor()
            return
        }
        if (!anchorTrusted) return

        try {
            val segment =
                ClockSegment.fromFormat(
                    epoch = begun.generation,
                    discontinuityGeneration = begun.generation,
                    inputSampleStart = 0L,
                    presentationUsStart = anchorUs,
                    sampleRateHz = begun.sampleRateHz,
                    speed = speed,
                    skippedInputSamples = 0L,
                )
            clock.append(segment)
            openSlope = segment.inputSamplesPerPresentationUs
            segmentsAppended++
        } catch (ignored: IllegalArgumentException) {
            // A segment the clock's invariants refuse is dropped; the clock keeps its last good segment.
        }
    }

    private fun distrustAnchor() {
        if (segmentsAppended > 0L) anchorTrusted = false
    }
}
