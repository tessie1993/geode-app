package dev.geode.pro

import dev.geode.model.StyleCatalogue

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
     * The one loose shader style that is free: Neon Chrome Sea, the app's first style for everyone
     * ([FIRST_STYLE_ID]), so the first thing a new listener sees is a showpiece they keep.
     */
    const val NEON_OPENER: String = "neon_chrome_sea"

    /**
     * Styles a free user can select and keep, the owner's ten: [NEON_OPENER] and the opener of the
     * first nine shader families in the picker's order (arbor, bulb, chrysanthemum, lucid, dive, glass,
     * nebula, terrain, truchet). Every dedicated renderer (silk, life, mycelium, morphing, fluid,
     * cymatics) and every other style is Pro: it shows a Pro badge, plays as a preview under the paywall
     * when picked, and is never kept, except the one a share earns ([ShareBonusRepository]). A renamed
     * or removed id simply stops matching, which makes that style Pro rather than crashing.
     */
    val freeStyleIds: Set<String> =
        setOf(
            NEON_OPENER,
            "arbor_grove",
            "bulb_classic",
            "chrys_bloom",
            "lucid_weave",
            "dive_lattice",
            "glass_orb",
            "nebula_dust",
            "terra_ridge",
            "truchet_arcs",
        )

    /** How many styles there are in all, for the paywall's comparison ("10 of 333"). */
    val styleCount: Int by lazy { StyleCatalogue.picker.values.sumOf { it.size } }

    /**
     * The style a fresh install opens on, for everyone: one free showpiece, not a Pro default that a
     * free user is then swapped off. It is the free neon opener, so it is in [freeStyleIds].
     */
    const val FIRST_STYLE_ID: String = "neon_chrome_sea"

    /**
     * Where a free user lands when the style on screen is no longer theirs (a saved look from a Pro
     * time, a trial that ran out): the first style. Must be in [freeStyleIds].
     */
    const val FALLBACK_STYLE_ID: String = FIRST_STYLE_ID

    /**
     * Customize's free tier, the owner's rule (issue #333): every category's Basics are free, and
     * everything below them is Pro. Basics are the two to four controls each category opens on
     * (core/viz/CustomizeMap marks them; the panel hands them in as [basics]), so a free user can
     * pick a palette, set the speed, turn the knobs and choose how strongly the music drives the
     * picture, and meets Pro only where the controls go deeper. A Pro control can still be tried:
     * it moves while held and springs back, then the paywall offers to keep it (ui/ProLock).
     */
    fun freeCustomizeControls(basics: Set<String>): Set<String> = basics

    /** Whether a Customize section is free: its Basics are, every other section is Pro. */
    fun customizeSectionFree(isBasics: Boolean): Boolean = isBasics

    /** Pro features given away in full. Empty: the free tier is the styles above plus basics. */
    val freeFeatures: Set<ProFeature> = emptySet()

    /** The largest short side (px) a free export may have: 1080p. */
    const val FREE_MAX_SHORT_SIDE: Int = 1080

    /** The highest frame rate a free export may have. */
    const val FREE_MAX_FPS: Int = 30

    /** The longest a non-Pro export may be: one minute, the rest of the track is cut. */
    const val FREE_MAX_EXPORT_MS: Long = 60_000L

    /** Visuals time (playing, on screen) after which a free user gets the one-time offer ([PaywallPrompts]). */
    const val TASTE_VISUALS_MS: Long = 30L * 60L * 1000L

    /** How long a free user must be away for the return to open the paywall ([PaywallPrompts]). */
    const val RETURN_AWAY_MS: Long = 5L * 60L * 1000L

    /** One hour, once per install, measured on the wall clock from the moment it is started. */
    const val TRIAL_DURATION_MS: Long = 60L * 60L * 1000L

    /** How far the wall clock may step back during the trial (a time correction) before that ends it. */
    const val TRIAL_CLOCK_SLACK_MS: Long = 2L * 60L * 1000L

    /**
     * Whether trial exports are held to the free limits: one minute, the watermark. On: the
     * trial is for trying every style, tool and resolution, and a finished, clean, full-length video
     * is the thing Pro is bought for.
     */
    const val TRIAL_EXPORTS_LIMITED: Boolean = true
}
