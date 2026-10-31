package dev.geode.audio

object AudioBus {
    /**
     * The live playback session's audio for the sound engine, for surfaces that don't hold the session
     * (the wallpaper, the dream). Set and cleared by PlaybackSession.
     */
    @Volatile
    var soundInput: dev.geode.engine.audio.SoundInput? = null
}
