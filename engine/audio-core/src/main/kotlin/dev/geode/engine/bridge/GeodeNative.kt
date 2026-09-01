package dev.geode.engine.bridge

object GeodeNative {
    init {
        System.loadLibrary("geode")
    }

    external fun version(): String

    external fun featureFrameFloats(): Int

    external fun analysisCreate(
        sampleRate: Int,
        fftSize: Int,
        hopRateHz: Float,
    ): Long

    external fun analysisDestroy(handle: Long)

    external fun analysisSetSampleRate(
        handle: Long,
        sampleRate: Int,
    )

    external fun analysisSetTuning(
        handle: Long,
        sensitivity: Float,
        refractoryMs: Float,
        attackSeconds: Float,
        releaseSeconds: Float,
    )

    external fun analysisReset(handle: Long)

    external fun analysisAnalyze(
        handle: Long,
        mid: FloatArray,
        side: FloatArray?,
        dtSeconds: Float,
        out: FloatArray,
    )

    external fun analysisPush(
        handle: Long,
        interleaved: FloatArray,
        frames: Int,
        channels: Int,
    )

    external fun analysisPull(
        handle: Long,
        out: FloatArray,
    ): Boolean

    external fun analysisKey(handle: Long): String

    external fun pulseReplay(
        flux: FloatArray,
        rms: FloatArray,
        hopRateHz: Float,
        sensitivity: Float,
        refractoryMs: Float,
        out: FloatArray,
    )

    external fun drumsCreate(
        bandCount: Int,
        hopRateHz: Float,
        sampleRate: Int,
    ): Long

    external fun drumsDestroy(handle: Long)

    external fun drumsStep(
        handle: Long,
        bands: FloatArray,
        out: FloatArray,
    )
}
