package dev.geode.engine.audio

sealed interface PresentationTime {
    data class At(
        val us: Long,
    ) : PresentationTime

    data class Skipped(
        val fromInputSample: Long,
        val toInputSample: Long,
    ) : PresentationTime

    data class StaleEpoch(
        val asked: Int,
        val current: Int,
    ) : PresentationTime

    data object Unknown : PresentationTime
}

sealed interface InputPosition {
    data class At(
        val inputSample: Long,
        val epoch: Int,
    ) : InputPosition

    data object Unknown : InputPosition
}
