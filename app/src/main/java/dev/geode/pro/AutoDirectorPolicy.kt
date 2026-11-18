package dev.geode.pro

/**
 * How strongly the auto-director drives the picture, 0 off .. 1 full, from the user's switch, the
 * entitlement and how far into the song playback is.
 *
 * Pro (and the trial) get the whole song. Free gets a taste: the first
 * [FreeTier.FREE_AUTO_DIRECTOR_TASTE_MS] of every song, so the feature can be felt before it is bought.
 * Past the taste the amount goes to 0 and the renderer eases the look back out over about a second
 * (core/viz/Director), so the end of the taste is a fade, not a cut. An export's rule (directed throughout
 * or not at all, by the switch, the entitlement and its length) is native: director::forExport.
 */
object AutoDirectorPolicy {
    /**
     * Live: [playerPlaying] says the app's own player is the source and playing. The taste is timed by
     * the song's position, so it only runs while the player plays: the mic and other apps' audio (which
     * pause the player) have no position to time a taste with, and get none.
     */
    fun amount(
        enabled: Boolean,
        gate: FeatureGate,
        positionMs: Long,
        playerPlaying: Boolean,
    ): Float =
        when {
            !enabled -> 0f
            gate.allows(ProFeature.AUTO_DIRECTOR) -> 1f
            playerPlaying && positionMs in 0 until FreeTier.FREE_AUTO_DIRECTOR_TASTE_MS -> 1f
            else -> 0f
        }
}
