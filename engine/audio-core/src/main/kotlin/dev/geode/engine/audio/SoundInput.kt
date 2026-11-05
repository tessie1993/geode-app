package dev.geode.engine.audio

/**
 * Where a surface's sound engine gets its audio: the shared [ring] and the rate of whatever is writing it
 * right now (the player's tap, the mic, another app's capture). Each renderer reads it through its own
 * [SoundCursor], so the screen, the wallpaper and the dream each hear every sample once.
 *
 * [playout] is set when a player's sink stands between the ring and the listener: the cursor then stops
 * at the frame the speaker plays now instead of the newest one written (#349). Null for live sources
 * (the mic, another app's audio), which are heard as they are written.
 */
class SoundInput(
    val ring: SampleRing,
    val playout: Playout? = null,
    private val rate: () -> Int,
) {
    val sampleRateHz: Int get() = rate()
}
