package dev.geode.audio

object AudioBus {
    /**
     * The live playback session's capture ring (mic, other apps' audio) for surfaces that don't hold the
     * session (the wallpaper, the dream). Kotlin until #485. Set and cleared by PlaybackSession.
     */
    @Volatile
    var soundInput: dev.geode.engine.audio.SoundInput? = null

    /**
     * The player's native ingest ring (`geode_ingest*`, `PlaybackEngine.playerIngest`), which every
     * renderer hands to its native side to hear the player (`VisualizerRenderer.soundSource`); 0 while no
     * playback session runs. Set and cleared by PlaybackSession.
     */
    @Volatile
    var playerIngest: Long = 0L
}
