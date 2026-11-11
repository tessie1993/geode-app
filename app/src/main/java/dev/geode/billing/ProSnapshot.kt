package dev.geode.billing

import android.content.Context
import dev.geode.pro.Entitlement
import dev.geode.pro.FeatureGate
import dev.geode.pro.ProFeature
import dev.geode.pro.TrialState

/**
 * A synchronous [FeatureGate] for the surfaces outside the UI's injection graph: the live
 * wallpaper and the screensaver. Both are started by the system, may run with no activity at all,
 * and must decide in their first callback whether to animate.
 *
 * Reads the same two prefs the repositories write, so a purchase or a trial is seen here the next
 * time the surface becomes visible. Debug builds' fake billing writes the same cache, so the debug
 * toggle reaches these surfaces too.
 */
object ProSnapshot {
    // The cached purchase is re-verified on every read, by the same rule as PlayBillingRepository,
    // because this is the reader that matters offline: the wallpaper and dream of a buyer away
    // from Play keep running on the signed purchase alone.
    private val verifier by lazy { PurchaseVerifier.forThisBuild() }

    fun gate(context: Context): FeatureGate {
        val prefs = PrefsEntitlementStore.open(context)
        val now = WallClock.SYSTEM.nowMs()
        if (PrefsEntitlementStore(prefs).load().vouchesForPro(verifier::isGenuine)) return FeatureGate(Entitlement.Pro)
        val trialStore = PrefsTrialStore(prefs)
        val trial = TrialState.at(trialStore.startedAtMs(), trialStore.endNoticeShown(), now)
        return if (trial is TrialState.Active) FeatureGate(Entitlement.Trial(trial.endsAtMs)) else FeatureGate.FREE
    }

    /**
     * The entitlement as the native surface run rule reads it: the wall-clock time [feature] may run
     * until. Pro is forever, a running trial its end, free 0. The rule itself, and when a trial's end
     * must be re-read, is native (`core/viz/SurfaceRun`).
     */
    fun animateUntilMs(
        context: Context,
        feature: ProFeature,
    ): Long {
        val gate = gate(context)
        if (!gate.allows(feature)) return 0L
        return (gate.entitlement as? Entitlement.Trial)?.endsAtMs ?: Long.MAX_VALUE
    }
}
