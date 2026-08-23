package dev.geode.engine.audio

fun interface PcmSink {
    fun write(
        interleaved: FloatArray,
        frameCount: Int,
        sourceChannelCount: Int,
    )
}
