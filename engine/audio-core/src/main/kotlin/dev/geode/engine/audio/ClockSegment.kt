package dev.geode.engine.audio

data class ClockSegment(
    val epoch: Int,
    val discontinuityGeneration: Int,
    val inputSampleStart: Long,
    val presentationUsStart: Long,
    val inputSamplesPerPresentationUs: Double,
    val speed: Float,
    val skippedInputSamples: Long,
) {
    init {
        require(inputSamplesPerPresentationUs > 0.0) {
            "slope must be positive, was $inputSamplesPerPresentationUs"
        }
        require(inputSampleStart >= 0) { "inputSampleStart must not be negative" }
        require(skippedInputSamples >= 0) { "skippedInputSamples must not be negative" }
    }

    val firstPresentedSample: Long get() = inputSampleStart + skippedInputSamples

    companion object {
        private const val US_PER_SECOND = 1_000_000.0

        fun fromFormat(
            epoch: Int,
            discontinuityGeneration: Int,
            inputSampleStart: Long,
            presentationUsStart: Long,
            sampleRateHz: Int,
            speed: Float,
            skippedInputSamples: Long = 0L,
        ): ClockSegment {
            require(sampleRateHz > 0) { "sampleRateHz must be positive, was $sampleRateHz" }
            require(speed > 0f) { "speed must be positive, was $speed" }
            return ClockSegment(
                epoch = epoch,
                discontinuityGeneration = discontinuityGeneration,
                inputSampleStart = inputSampleStart,
                presentationUsStart = presentationUsStart,
                inputSamplesPerPresentationUs = sampleRateHz * speed / US_PER_SECOND,
                speed = speed,
                skippedInputSamples = skippedInputSamples,
            )
        }
    }
}
