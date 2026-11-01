package dev.geode.engine.audioandroid

fun interface SkippedFrameSource {
    fun skippedInputFramesSinceFlush(): Long
}

interface SinkClockHooks {
    fun onSpeedApplied(speed: Float)

    fun onSkipSilenceApplied(enabled: Boolean)

    fun attachSkippedFrames(source: SkippedFrameSource)

    data object None : SinkClockHooks {
        override fun onSpeedApplied(speed: Float) = Unit

        override fun onSkipSilenceApplied(enabled: Boolean) = Unit

        override fun attachSkippedFrames(source: SkippedFrameSource) = Unit
    }
}

fun interface TapBoundaryListener {
    fun onTapBoundary(
        ended: PcmTapFormat?,
        endedFrames: Long,
        begun: PcmTapFormat,
    )
}
