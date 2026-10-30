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

    fun allows(feature: ProFeature): Boolean =
        when (entitlement) {
            Entitlement.Pro -> true
            is Entitlement.Trial -> feature != ProFeature.NO_WATERMARK || FreeTier.TRIAL_REMOVES_WATERMARK
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

    /** Whether a finished export carries the corner wordmark. */
    val watermarksExports: Boolean get() = !allows(ProFeature.NO_WATERMARK)

    companion object {
        val FREE: FeatureGate = FeatureGate(Entitlement.Free)
    }
}
