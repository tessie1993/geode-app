package dev.geode.pro

import dev.geode.render.scene.ParamKeys
import dev.geode.render.scene.SceneCapabilities
import dev.geode.render.scene.SceneIds
import dev.geode.render.scene.StyleFamilies

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
     * Every style-family style but its opener. The native catalogue says which style opens a family
     * (core/viz/Catalogue); this file is what makes the opener the free one.
     */
    val familyProIds: Set<String> = StyleFamilies.all.flatMapTo(mutableSetOf()) { it.ids - it.opener }

    /**
     * Styles a free user can select and keep: the fragment-shader styles (julia, tunnel, bars,
     * ring, scope, plasma, nebula and the rest of [SceneCapabilities.SHADER_SCENES]), so a loose
     * shader style added later is free too, minus [familyProIds]. A style family gives away its
     * opener (the first, most basic style) and keeps the other eleven for Pro: a free user can try every
     * family, and buying Pro is what opens the range. Everything else (silk, fluid, water, MilkDrop,
     * cymatics and the other dedicated renderers) is Pro, shows a Pro badge and opens the paywall
     * when picked. A renamed or removed id simply stops matching, which makes that style Pro rather
     * than crashing.
     */
    val freeStyleIds: Set<String> = SceneCapabilities.SHADER_SCENES.keys - familyProIds

    /**
     * Where a free user lands when the style on screen is no longer theirs (a new install, whose
     * default is silk; a trial that ran out). Must be in [freeStyleIds].
     */
    const val FALLBACK_STYLE_ID: String = SceneIds.BARS

    /**
     * Customize › Look's one free control: the palette. Colour is the first thing a listener
     * reaches for and the change they see at once, so it is the taste of Look that sells the rest.
     * Customize › Macros is free in full and needs no entry; Advanced has no free control at all.
     */
    const val FREE_LOOK_CONTROL: String = ParamKeys.PALETTE

    /**
     * Customize › Events' one free control: the one-tap reaction recipes ("Kick punch"). They are
     * the simple way into reactions; the strength slider and the route editor behind them are Pro.
     */
    const val FREE_EVENTS_CONTROL: String = "Event recipes"

    /**
     * Customize › (style name)'s one free control: the first of these the style on screen shows.
     * A style's page holds different controls per style, so a single key could leave a cymatics
     * plate or a fluid with nothing free; the list runs from the most style-specific to
     * Kaleidoscope, which every style has, so exactly one control is always free.
     */
    val FREE_STYLE_CONTROL_ORDER: List<String> =
        listOf(
            ParamKeys.GEOMETRY,
            ParamKeys.FLUID_CURL,
            ParamKeys.RIPPLE_STRENGTH,
            ParamKeys.PARTICLE_SHAPE,
            ParamKeys.BLEND_PRESET_CHANGES,
            ParamKeys.KALEIDOSCOPE,
        )

    /** The style page's free control, given which controls are live on the style ([isShown]). */
    fun freeStyleControl(isShown: (String) -> Boolean): String? = FREE_STYLE_CONTROL_ORDER.firstOrNull(isShown)

    /** Every Customize control a free user may change on this style, Macros aside. */
    fun freeCustomizeControls(isShown: (String) -> Boolean): Set<String> =
        setOfNotNull(FREE_LOOK_CONTROL, FREE_EVENTS_CONTROL, freeStyleControl(isShown))

    /** Pro features given away in full. Empty: the free tier is the styles above plus basics. */
    val freeFeatures: Set<ProFeature> = emptySet()

    /** The largest short side (px) a free export may have: 1080p. */
    const val FREE_MAX_SHORT_SIDE: Int = 1080

    /** The highest frame rate a free export may have. */
    const val FREE_MAX_FPS: Int = 30

    /** The longest a non-Pro export may be: one minute, the rest of the track is cut. */
    const val FREE_MAX_EXPORT_MS: Long = 60_000L

    /**
     * How much of each song the auto-director directs for free: the taste. It matches the free export cap,
     * so a free export (at most this long) is directed throughout.
     */
    const val FREE_AUTO_DIRECTOR_TASTE_MS: Long = 60_000L

    /** One hour, once per install, measured on the wall clock from the moment it is started. */
    const val TRIAL_DURATION_MS: Long = 60L * 60L * 1000L

    /**
     * Whether trial exports are held to the free limits: one minute, full-frame watermark. On: the
     * trial is for trying every style, tool and resolution, and a finished, clean, full-length video
     * is the thing Pro is bought for.
     */
    const val TRIAL_EXPORTS_LIMITED: Boolean = true
}
