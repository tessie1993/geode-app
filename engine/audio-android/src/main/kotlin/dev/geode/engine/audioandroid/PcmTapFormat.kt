package dev.geode.engine.audioandroid

import androidx.media3.common.C

enum class PcmSampleWidth(
    val bytes: Int,
) {
    SIGNED_16(Short.SIZE_BYTES),
    FLOAT_32(Float.SIZE_BYTES),
}

data class PcmTapFormat(
    val sampleRateHz: Int,
    val channelCount: Int,
    val encoding: Int,
    val generation: Int,
) {
    val sampleWidth: PcmSampleWidth? =
        when (encoding) {
            C.ENCODING_PCM_16BIT -> PcmSampleWidth.SIGNED_16
            C.ENCODING_PCM_FLOAT -> PcmSampleWidth.FLOAT_32
            else -> null
        }
}
