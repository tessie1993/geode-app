package dev.geode.billing

import android.content.Context
import dev.geode.pro.Entitlement
import dev.geode.pro.FeatureGate
import dev.geode.pro.TrialState

/**
 * A synchronous [FeatureGate] for the code outside the UI's injection graph: Cast, an export and the
 * auto-director feed, which must decide without waiting on the billing repository.
 *
 * Reads the same two prefs the repositories write, so a purchase or a trial is seen here the next
 * time they ask. Debug builds' fake billing writes the same cache, so the debug toggle reaches them too.
 */
object ProSnapshot {
    // The cached purchase is re-verified on every read, by the same rule as PlayBillingRepository,
    // because this is the reader that matters offline: a buyer away from Play keeps running on the signed
    // purchase alone.
    private val verifier by lazy { PurchaseVerifier.forThisBuild() }

    fun gate(context: Context): FeatureGate {
        val prefs = PrefsEntitlementStore.open(context)
        val now = WallClock.SYSTEM.nowMs()
        if (PrefsEntitlementStore(prefs).load().vouchesForPro(verifier::isGenuine)) return FeatureGate(Entitlement.Pro)
        val trialStore = PrefsTrialStore(prefs)
        val trial = TrialState.at(trialStore.startedAtMs(), trialStore.endNoticeShown(), now, trialStore.lastSeenMs())
        return if (trial is TrialState.Active) FeatureGate(Entitlement.Trial(trial.endsAtMs)) else FeatureGate.FREE
    }
}
