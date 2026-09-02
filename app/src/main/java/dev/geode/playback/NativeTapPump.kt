package dev.geode.playback

import androidx.media3.common.C
import dev.geode.engine.audioandroid.PcmTap
import dev.geode.engine.audioandroid.SinkClockDriver
import dev.geode.engine.bridge.GeodeNative
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Drains the native mixer's tap into the same [PcmTap] the Media3 chain feeds, so analysis and the
 * presentation clock see the native player exactly as they see ExoPlayer.
 */
class NativeTapPump(
    private val tap: PcmTap,
    private val clock: SinkClockDriver,
) {
    @Volatile
    private var running = false
    private var thread: Thread? = null

    fun start(handle: Long) {
        running = true
        thread =
            Thread({ loop(handle) }, "geode-native-tap").apply {
                isDaemon = true
                start()
            }
    }

    /** Returns once the thread has let go of the handle; call before the player is destroyed. */
    fun stop() {
        running = false
        thread?.join()
        thread = null
    }

    private fun loop(handle: Long) {
        val buffer = ByteBuffer.allocateDirect(FRAMES * CHANNELS * Float.SIZE_BYTES).order(ByteOrder.nativeOrder())
        var rate = 0
        clock.attachSkippedFrames { 0L }
        while (running) {
            val current = GeodeNative.playerOutputSampleRate(handle)
            if (current > 0 && current != rate) {
                rate = current
                // The clock driver trusts a boundary only after both sink hooks reported; native has neither.
                clock.onSpeedApplied(1f)
                clock.onSkipSilenceApplied(false)
                tap.flush(rate, CHANNELS, C.ENCODING_PCM_FLOAT)
            }
            val frames = if (rate > 0) GeodeNative.playerReadTap(handle, buffer, FRAMES) else 0
            if (frames > 0) {
                buffer.limit(frames * CHANNELS * Float.SIZE_BYTES)
                buffer.position(0)
                tap.handleBuffer(buffer)
            } else {
                Thread.sleep(IDLE_SLEEP_MS)
            }
        }
    }

    private companion object {
        const val FRAMES = 2048
        const val CHANNELS = 2
        const val IDLE_SLEEP_MS = 10L
    }
}
