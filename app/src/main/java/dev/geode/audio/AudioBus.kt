package dev.geode.audio

import dev.geode.engine.bridge.CastNative
import dev.geode.engine.bridge.IngestNative
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

object AudioBus {
    private val ring = MutableStateFlow(IngestNative.pcmRingLive())

    /**
     * The phone's output mix as a ring ([OutputMixCapture]) while the user has the app hear other apps; 0
     * otherwise. Set and cleared by the capture controller; the cast's render thread hears it too (native).
     */
    @Volatile
    var deviceInput: Long = 0L
        set(value) {
            field = value
            CastNative.castRenderSetDeviceRing(value)
            publish()
        }

    /** The ring a renderer hears now: the phone's mix while it is on, else the player's (native holds it while a session does). */
    val heard: StateFlow<Long> = ring.asStateFlow()

    /** A playback session holds the player's ring ([on]) or lets it go. */
    fun sessionLive(on: Boolean) {
        IngestNative.pcmRingSetLive(on)
        publish()
    }

    @Synchronized
    private fun publish() {
        ring.value = deviceInput.takeIf { it != 0L } ?: IngestNative.pcmRingLive()
    }
}
