package dev.geode.billing

/**
 * When an ad is allowed to appear.
 *
 * There is exactly one moment, and it is not the one people reach for first. Google Play's Better
 * Ads Experiences policy names "ads that appear at the beginning of a content segment" as the
 * violating case, and says users may expect an ad "at the end of a … content segment". A track is
 * a content segment, so an ad *between* tracks is compliant only if it fires when the finished
 * track ends — never as the next one starts.
 *
 * Modelled as a type with one member rather than a boolean so the violating placement has no
 * spelling. There is no `BeforeTrack`, so no code can request one.
 */
sealed interface AdMoment {
    /**
     * The previous track has finished and the next has not begun.
     *
     * The player is paused at this point rather than playing under the ad: an ad talking over
     * someone's music is disruptive whatever the policy says about placement.
     */
    data object TrackFinished : AdMoment
}

/**
 * The rules an ad presentation has to satisfy, from Google Play's Ads policy.
 *
 * These are not preferences. "If your app displays ads … that interfere with normal use, they must
 * be easily dismissible without penalty", and a non-closeable full-screen interstitial may not run
 * past fifteen seconds. Encoding them here means the presenter cannot quietly drift out of
 * compliance.
 */
object AdPolicy {
    /**
     * The hard ceiling on a full-screen ad that cannot be closed.
     *
     * We do not rely on it: [MIN_DISMISSIBLE_AFTER_MS] closes the ad long before this. It exists as
     * the number to assert against if a network ever hands us an undismissable creative.
     */
    const val NON_CLOSEABLE_LIMIT_MS = 15_000L

    /**
     * How long the dismiss control may be withheld.
     *
     * Long enough for the ad to register as an impression, short enough that it never reads as a
     * trap. Anything above this and the ad is interfering with normal use.
     */
    const val MIN_DISMISSIBLE_AFTER_MS = 5_000L

    /**
     * The least time between two ads.
     *
     * Without a floor, a queue of one-minute tracks becomes an ad every minute, which is the
     * behaviour that gets an app pulled rather than merely disliked.
     */
    const val MIN_INTERVAL_MS = 3 * 60_000L

    /** Tracks shorter than this do not earn an ad — the ratio would be indefensible. */
    const val MIN_TRACK_MS = 45_000L

    /**
     * Whether an ad may run now.
     *
     * [elapsedSinceLastAdMs] is wall-clock since the previous ad, not a track count: someone
     * skipping through a playlist should not be charged an ad per skip.
     */
    fun mayShow(
        tier: Tier,
        moment: AdMoment,
        finishedTrackMs: Long,
        elapsedSinceLastAdMs: Long,
    ): Boolean {
        if (!tier.showsAds) return false
        return when (moment) {
            AdMoment.TrackFinished ->
                finishedTrackMs >= MIN_TRACK_MS && elapsedSinceLastAdMs >= MIN_INTERVAL_MS
        }
    }
}
