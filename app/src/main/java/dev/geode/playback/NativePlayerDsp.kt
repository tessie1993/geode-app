package dev.geode.playback

import dev.geode.audio.dsp.NativeDspProcessor
import dev.geode.engine.bridge.GeodeNative

/**
 * Mirrors the equalizer settings the Media3 processor holds onto a chain built at the native player's
 * output rate. Every call runs on the main thread.
 */
class NativePlayerDsp(
    private val processor: NativeDspProcessor,
) {
    private var player = 0L
    private var handle = 0L
    private var rate = 0

    fun attach(player: Long) {
        this.player = player
        processor.onSettings = { settings ->
            val h = handle
            if (h != 0L) processor.apply(h, settings)
        }
    }

    /** Rebuilds the chain when the output rate changed; the player lets go of the old one before it is freed. */
    fun sync() {
        val current = GeodeNative.playerOutputSampleRate(player)
        if (current <= 0 || current == rate) return
        val next = GeodeNative.dspCreate(current, CHANNELS)
        if (next != 0L) processor.apply(next, processor.settings)
        GeodeNative.playerSetDsp(player, next)
        if (handle != 0L) GeodeNative.dspDestroy(handle)
        handle = next
        rate = current
    }

    fun release() {
        processor.onSettings = null
        if (player != 0L) GeodeNative.playerSetDsp(player, 0L)
        if (handle != 0L) GeodeNative.dspDestroy(handle)
        handle = 0L
        rate = 0
        player = 0L
    }

    private companion object {
        const val CHANNELS = 2
    }
}
