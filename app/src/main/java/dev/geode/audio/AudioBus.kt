package dev.geode.audio

import dev.geode.engine.bridge.IngestNative

object AudioBus {
    /**
     * The phone's output mix as a ring ([OutputMixCapture]) while the user has the app hear other apps; 0
     * otherwise. Set and cleared by the capture controller.
     */
    @Volatile
    var deviceInput: Long = 0L

    /** The ring a renderer hears now: the phone's mix while it is on, else the player's (native holds it while a session does). */
    val heard: Long get() = deviceInput.takeIf { it != 0L } ?: IngestNative.pcmRingLive()
}
