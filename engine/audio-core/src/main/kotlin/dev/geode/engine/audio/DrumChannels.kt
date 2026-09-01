package dev.geode.engine.audio

import dev.geode.engine.bridge.GeodeNative

class DrumChannels(
    bandCount: Int,
    hopRateHz: Float,
    sampleRateHz: Int,
) : AutoCloseable {
    private var handle: Long = GeodeNative.drumsCreate(bandCount, hopRateHz, sampleRateHz)
    private val impulses = FloatArray(3)

    var kick: Float = 0f
        private set

    var snare: Float = 0f
        private set

    var hat: Float = 0f
        private set

    fun step(bands: FloatArray) {
        GeodeNative.drumsStep(handle, bands, impulses)
        kick = impulses[0]
        snare = impulses[1]
        hat = impulses[2]
    }

    override fun close() {
        if (handle != 0L) {
            GeodeNative.drumsDestroy(handle)
            handle = 0L
        }
    }
}
