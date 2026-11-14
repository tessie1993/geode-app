package dev.geode.audio

object AudioBus {
    /**
     * The live playback session's ring handle for the sound engine, for surfaces that don't hold the
     * session (the wallpaper, the dream); 0 while there is none. Set and cleared by PlaybackSession.
     */
    @Volatile
    var soundInput: Long = 0L
}
