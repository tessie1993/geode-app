package dev.geode.engine.audio

/**
 * Where a surface's sound engine gets its audio: the shared [ring] and the rate of whatever is writing it
 * right now (the player's tap, the mic, another app's capture). Each renderer reads it through its own
 * [SoundCursor], so the screen, the wallpaper and the dream each hear every sample once.
 */
class SoundInput(
    val ring: SampleRing,
    private val rate: () -> Int,
) {
    val sampleRateHz: Int get() = rate()
}
