package dev.geode.billing

import android.content.Context
import dev.geode.pro.Entitlement
import dev.geode.pro.FeatureGate
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
    fun gate(context: Context): FeatureGate {
        val prefs = PrefsEntitlementStore.open(context)
        val now = WallClock.SYSTEM.nowMs()
        if (PrefsEntitlementStore(prefs).load().vouchesForPro()) return FeatureGate(Entitlement.Pro)
        val trialStore = PrefsTrialStore(prefs)
        val trial = TrialState.at(trialStore.startedAtMs(), trialStore.endNoticeShown(), now)
        return if (trial is TrialState.Active) FeatureGate(Entitlement.Trial(trial.endsAtMs)) else FeatureGate.FREE
    }
}
