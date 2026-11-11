package dev.geode.pro

import dev.geode.model.WatermarkChoice

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

    /** Whether a finished export must carry the wordmark (where is the user's: Settings › Preferences). */
    val watermarksExports: Boolean get() = !allows(ProFeature.NO_WATERMARK)

    /** The longest an export may run, or null for no limit. */
    val exportCapMs: Long? get() = if (allows(ProFeature.FULL_LENGTH_EXPORT)) null else FreeTier.FREE_MAX_EXPORT_MS

    /**
     * What a render under this entitlement may produce, as one value the render itself applies
     * (`RecordController`, which reads it when a recording starts), so no entry point can forget the
     * cap or the wordmark, which is how a trial take once came out clean and full length (#354).
     */
    val renderLimits: RenderLimits get() = RenderLimits(exportCapMs, WatermarkChoice(required = watermarksExports))

    companion object {
        val FREE: FeatureGate = FeatureGate(Entitlement.Free)

        /** What separates a Pro export from a trial one. */
        private val EXPORT_LIMITS = setOf(ProFeature.NO_WATERMARK, ProFeature.FULL_LENGTH_EXPORT)
    }
}

/**
 * The export limits of one entitlement: the longest a render may run ([capMs], null for no limit), and
 * its wordmark ([WatermarkChoice.required] by the entitlement; placed as the user's [withMark] says).
 */
data class RenderLimits(
    val capMs: Long?,
    val watermark: WatermarkChoice,
) {
    /** These limits with the user's watermark preference ([WatermarkChoice.required] stays the entitlement's). */
    fun withMark(preference: WatermarkChoice): RenderLimits = copy(watermark = preference.copy(required = watermark.required))

    companion object {
        /** Pro's: full length, no wordmark. */
        val NONE: RenderLimits = RenderLimits(capMs = null, watermark = WatermarkChoice.NONE)
    }
}
