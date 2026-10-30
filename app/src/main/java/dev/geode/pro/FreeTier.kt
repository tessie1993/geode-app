package dev.geode.pro

import dev.geode.render.scene.SceneIds

/**
 * The shape of the free tier, in one place, meant to be edited by hand.
 *
 * Every gate in the app asks [FeatureGate], and [FeatureGate] answers from this object, so
 * widening or narrowing what a free user gets is a change to this file alone — no screen, export
 * path or service has its own opinion. Add a style id to [freeStyleIds] to make that style free;
 * add a [ProFeature] to [freeFeatures] to give it away entirely.
 */
object FreeTier {
    /**
     * Styles a free user can select and keep. Everything else shows a Pro badge and opens the
     * paywall when picked. Ids come from [SceneIds]; a renamed or removed id simply stops matching,
     * which makes that style Pro rather than crashing.
     */
    val freeStyleIds: Set<String> =
        setOf(
            SceneIds.DEFAULT,
            SceneIds.BARS,
            SceneIds.RING,
        )

    /** Pro features given away in full. Empty: the free tier is the styles above plus basics. */
    val freeFeatures: Set<ProFeature> = emptySet()

    /** The largest short side (px) a free export may have: 1080p. */
    const val FREE_MAX_SHORT_SIDE: Int = 1080

    /** The highest frame rate a free export may have. */
    const val FREE_MAX_FPS: Int = 30

    /** One hour, once per install, measured on the wall clock from the moment it is started. */
    const val TRIAL_DURATION_MS: Long = 60L * 60L * 1000L

    /**
     * Whether exports made during the trial are clean. Off: the trial is for trying every style and
     * tool, and a clean 4K render is the thing Pro is bought for, so trial renders keep the mark.
     */
    const val TRIAL_REMOVES_WATERMARK: Boolean = false
}
