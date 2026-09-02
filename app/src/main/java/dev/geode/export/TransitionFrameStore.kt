package dev.geode.export

import java.nio.ByteBuffer

/** The last frame one clip rendered, handed from its capture effect to the transition opening the next clip. */
class TransitionFrameStore {
    @Volatile
    var frame: CapturedFrame? = null

    class CapturedFrame(
        val width: Int,
        val height: Int,
        val rgba: ByteBuffer,
    )
}
