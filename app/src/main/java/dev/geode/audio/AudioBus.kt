package dev.geode.audio

object AudioBus {
    /**
     * The live playback session's ring handle for the sound engine, for surfaces that don't hold the
     * session (the wallpaper, the dream); 0 while there is none. Set and cleared by PlaybackSession.
     */
    @Volatile
    var soundInput: Long = 0L

    /**
     * The phone's output mix as a ring ([OutputMixCapture]) while the user has the app hear other apps; 0
     * otherwise. Set and cleared by the capture controller.
     */
    @Volatile
    var deviceInput: Long = 0L

    /** The ring a renderer hears now: the phone's mix while it is on, else the player's. */
    val heard: Long get() = deviceInput.takeIf { it != 0L } ?: soundInput
}
