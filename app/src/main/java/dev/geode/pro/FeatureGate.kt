package dev.geode.pro

/** Where the user's access comes from right now. */
sealed interface Entitlement {
    data object Free : Entitlement

    /** The one-hour trial is running and ends at [endsAtMs] (wall clock). */
    data class Trial(
        val endsAtMs: Long,
    ) : Entitlement

    data object Pro : Entitlement
}

/**
 * The single answer to "may the user do this?".
 *
 * Built only by [GetFeatureGateUseCase]; every gate in the app reads one of these rather than
 * looking at billing or the trial itself, so the rules below are the whole policy.
 */
data class FeatureGate(
    val entitlement: Entitlement,
) {
    val isPro: Boolean get() = entitlement == Entitlement.Pro

    /**
     * How long until this gate's answers change on their own, or null when they never do: only a
     * running trial ends without anyone acting. Surfaces that run with no app around them (the live
     * wallpaper, the dream) read the gate once per change they hear about, and no broadcast marks
     * the trial's end, so they wake themselves this long from [nowMs] and read it again (#376).
     * Zero once the end has passed, so a late reader re-checks at once.
     */
    fun msUntilTrialEnds(nowMs: Long): Long? =
        (entitlement as? Entitlement.Trial)?.let { (it.endsAtMs - nowMs).coerceAtLeast(0L) }

    fun allows(feature: ProFeature): Boolean =
        when (entitlement) {
            Entitlement.Pro -> true
            is Entitlement.Trial -> feature !in EXPORT_LIMITS || !FreeTier.TRIAL_EXPORTS_LIMITED
            Entitlement.Free -> feature in FreeTier.freeFeatures
        }

    fun allowsStyle(sceneId: String): Boolean = sceneId in FreeTier.freeStyleIds || allows(ProFeature.PREMIUM_STYLES)

    /**
     * The feature that blocks an export of this size and rate, or null when it may run.
     * Resolution is reported before frame rate: it is the bigger of the two upgrades.
     */
    fun blockingExportFeature(
        shortSide: Int,
        fps: Int,
    ): ProFeature? =
        when {
            shortSide > FreeTier.FREE_MAX_SHORT_SIDE && !allows(ProFeature.EXPORT_4K) -> ProFeature.EXPORT_4K
            fps > FreeTier.FREE_MAX_FPS && !allows(ProFeature.EXPORT_60FPS) -> ProFeature.EXPORT_60FPS
            else -> null
        }

    /** Whether a finished export carries the full-frame wordmark. */
    val watermarksExports: Boolean get() = !allows(ProFeature.NO_WATERMARK)

    /** The longest an export may run, or null for no limit. */
    val exportCapMs: Long? get() = if (allows(ProFeature.FULL_LENGTH_EXPORT)) null else FreeTier.FREE_MAX_EXPORT_MS

    /**
     * What a render under this entitlement may produce, as one value the render itself applies
     * (`ExportController.renderProject`). Every project render (a stopped take, Studio's
     * Render) reads this where it runs, so no entry point can forget the cap or the wordmark, which
     * is how a trial take once came out clean and full length (#354).
     */
    val renderLimits: RenderLimits get() = RenderLimits(exportCapMs, watermarksExports)

    companion object {
        val FREE: FeatureGate = FeatureGate(Entitlement.Free)

        /** What separates a Pro export from a trial one. */
        private val EXPORT_LIMITS = setOf(ProFeature.NO_WATERMARK, ProFeature.FULL_LENGTH_EXPORT)
    }
}

/** The export limits of one entitlement: the longest a render may run ([capMs], null for no limit), and whether it carries the wordmark. */
data class RenderLimits(
    val capMs: Long?,
    val watermark: Boolean,
) {
    companion object {
        /** Pro's: full length, no wordmark. */
        val NONE: RenderLimits = RenderLimits(capMs = null, watermark = false)
    }
}
