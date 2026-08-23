package dev.geode.engine.audio

sealed interface RingReadResult {
    data class Ok(
        val firstSample: Long,
        val sampleCount: Int,
        val epoch: Int,
    ) : RingReadResult

    data class Gap(
        val requested: Long,
        val oldestAvailable: Long,
    ) : RingReadResult

    data class Discontinuity(
        val cursorEpoch: Int,
        val currentEpoch: Int,
    ) : RingReadResult

    data object NotYetAvailable : RingReadResult
}
