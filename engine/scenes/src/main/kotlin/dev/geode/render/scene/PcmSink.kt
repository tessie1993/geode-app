package dev.geode.render.scene

interface PcmSink {
    fun acceptPcm(
        samples: FloatArray,
        count: Int,
    )
}

class PcmChunk(
    val data: FloatArray,
    val count: Int,
)
