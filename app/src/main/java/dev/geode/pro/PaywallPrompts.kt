package dev.geode.pro

/** Where [PaywallPrompts] keeps what must outlive the process; an interface so tests keep it in memory. */
interface PromptStore {
    /** Cold starts since install, this one included once [PaywallPrompts] is built. */
    fun sessions(): Int

    fun saveSessions(count: Int)

    /** Time with Visuals on screen while music plays, summed over every session. */
    fun visualsMs(): Long

    fun saveVisualsMs(ms: Long)

    fun milestoneShown(): Boolean

    fun saveMilestoneShown()

    /** When the app last went to the background (wall clock), 0 if it has not since the last return. */
    fun stoppedAtMs(): Long

    fun saveStoppedAt(ms: Long)
}

/**
 * The paywalls nobody tapped for, the owner's rules in one place (the numbers are [FreeTier]'s):
 *
 * - The taste milestone: once [FreeTier.TASTE_VISUALS_MS] of Visuals has played, the offer opens at the
 *   next break (the song ends or the music pauses), never mid-song, once per install.
 * - The return: coming back after [FreeTier.RETURN_AWAY_MS] or more away opens the paywall, every time,
 *   but never in the first session, and never on the way back from something the app itself opened
 *   (a picker, the share sheet), which [onOutboundLaunch] marks.
 *
 * Both are for free users only: Pro and the trial never see them. Which paywall wins when two ask at
 * once is [PaywallNavigator]'s rule. The time the app left is kept in [PromptStore] on the wall clock
 * ([nowMs]), so a return still counts after the process was ended or the phone restarted while away.
 */
class PaywallPrompts(
    private val store: PromptStore,
    private val navigator: PaywallNavigator,
    private val nowMs: () -> Long,
) {
    private var unsavedVisualsMs = 0L
    private var outbound = false
    private var leftForOwnLaunch = false

    init {
        store.saveSessions(store.sessions() + 1)
    }

    /** Visuals were on screen with music playing, in the foreground, for [ms]. */
    @Synchronized
    fun onVisualsTime(
        ms: Long,
        gate: FeatureGate,
    ) {
        if (!gate.isFree() || store.milestoneShown() || ms <= 0L) return
        unsavedVisualsMs += ms
        if (unsavedVisualsMs >= SAVE_EVERY_MS) flush()
    }

    /** A break in the music: the song ended or changed, or playback paused. */
    @Synchronized
    fun onBreak(gate: FeatureGate) {
        if (!gate.isFree() || store.milestoneShown()) return
        if (store.visualsMs() + unsavedVisualsMs < FreeTier.TASTE_VISUALS_MS) return
        flush()
        // A paywall the user opened outranks the offer; it is kept for the next break then.
        if (navigator.open(PaywallTrigger.TasteMilestone)) store.saveMilestoneShown()
    }

    /** The app is about to open another app's screen itself; the return from it is not a return. */
    @Synchronized
    fun onOutboundLaunch() {
        outbound = true
    }

    /**
     * The activity is in front and interactive again. A launch that never took the app off the screen
     * (the share sheet sits over it, so the app only pauses) is forgotten here, before it could mark a
     * later, real absence as the app's own.
     */
    @Synchronized
    fun onResumed() {
        outbound = false
    }

    /** The app left the screen. Whether it left for a screen it opened itself is taken now, and cleared. */
    @Synchronized
    fun onBackground() {
        flush()
        leftForOwnLaunch = outbound
        outbound = false
        store.saveStoppedAt(nowMs())
    }

    @Synchronized
    fun onForeground(gate: FeatureGate) {
        val left = store.stoppedAtMs()
        if (left <= 0L) return
        store.saveStoppedAt(0L)
        val fromOwnLaunch = leftForOwnLaunch
        leftForOwnLaunch = false
        if (fromOwnLaunch || !gate.isFree() || store.sessions() < 2) return
        if (nowMs() - left < FreeTier.RETURN_AWAY_MS) return
        navigator.open(PaywallTrigger.Returned)
    }

    private fun flush() {
        if (unsavedVisualsMs == 0L) return
        store.saveVisualsMs(store.visualsMs() + unsavedVisualsMs)
        unsavedVisualsMs = 0L
    }

    private fun FeatureGate.isFree(): Boolean = entitlement == Entitlement.Free

    private companion object {
        /** Visuals time is written in steps this long, not on every tick. */
        const val SAVE_EVERY_MS = 30_000L
    }
}
