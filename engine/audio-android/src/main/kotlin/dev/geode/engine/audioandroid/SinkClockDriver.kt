package dev.geode.engine.audioandroid

import dev.geode.engine.audio.AudioPresentationClock
import dev.geode.engine.audio.ClockSegment
import kotlin.math.roundToLong

class SinkClockDriver(
    private val clock: AudioPresentationClock,
) : SinkClockHooks, TapBoundaryListener {
    @Volatile
    private var skipped: SkippedFrameSource = SkippedFrameSource { 0L }

    private var speed: Float = 1f
    private var sawSpeedHook = false
    private var sawSkipHook = false
    private var speedAuthoritative = false

    private var anchorUs: Long = 0L

    private var openSlope: Double = 0.0

    private var anchorTrusted = true

    private var boundaries = 0L
    private var segmentsAppended = 0L
    private var hookedBoundaries = 0L
    private var refusedSpeedNotAuthoritative = 0L
    private var refusedUnreadableFormat = 0L
    private var refusedUntrustedAnchor = 0L
    private var discardedSkipExceedingFrames = 0L
    private var refusedByClockInvariant = 0L
    private var unmeasuredBoundaries = 0L

    val diagnostics: SinkClockDiagnostics
        get() =
            SinkClockDiagnostics(
                boundaries = boundaries,
                segmentsAppended = segmentsAppended,
                hookedBoundaries = hookedBoundaries,
                refusedSpeedNotAuthoritative = refusedSpeedNotAuthoritative,
                refusedUnreadableFormat = refusedUnreadableFormat,
                refusedUntrustedAnchor = refusedUntrustedAnchor,
                discardedSkipExceedingFrames = discardedSkipExceedingFrames,
                refusedByClockInvariant = refusedByClockInvariant,
                unmeasuredBoundaries = unmeasuredBoundaries,
                skippedFramesAttached = skippedFramesAttached,
                anchorTrusted = anchorTrusted,
            )

    @Volatile
    private var skippedFramesAttached = false

    override fun attachSkippedFrames(source: SkippedFrameSource) {
        skipped = source
        skippedFramesAttached = true
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
        boundaries++
        if (sawSkipHook) hookedBoundaries++

        val rawSkip = if (ended != null && endedFrames > 0L) skipped.skippedInputFramesSinceFlush() else 0L
        val trustworthy = rawSkip in 0L..endedFrames
        if (!trustworthy) discardedSkipExceedingFrames++
        val skip = if (trustworthy) rawSkip else 0L

        if (openSlope > 0.0) {
            anchorUs += ((endedFrames - skip) / openSlope).roundToLong()
        } else if (ended != null && endedFrames > 0L) {
            unmeasuredBoundaries++
            distrustAnchor()
        }

        if (sawSkipHook) speedAuthoritative = sawSpeedHook
        sawSpeedHook = false
        sawSkipHook = false

        openSlope = 0.0
        if (!speedAuthoritative) {
            refusedSpeedNotAuthoritative++
            distrustAnchor()
            return
        }
        if (begun.sampleRateHz <= 0 || begun.channelCount <= 0 || begun.sampleWidth == null || !(speed > 0f)) {
            refusedUnreadableFormat++
            distrustAnchor()
            return
        }
        if (!anchorTrusted) {
            refusedUntrustedAnchor++
            return
        }

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
        } catch (unexpected: IllegalArgumentException) {
            refusedByClockInvariant++
        }
    }

    private fun distrustAnchor() {
        if (segmentsAppended > 0L) anchorTrusted = false
    }
}
