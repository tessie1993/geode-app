package dev.geode.pro

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class PaywallPromptsTest {
    private var now = 1_000L
    private val navigator = PaywallNavigator()
    private val free = FeatureGate.FREE
    private val trial = FeatureGate(Entitlement.Trial(endsAtMs = Long.MAX_VALUE))

    private fun prompts(store: InMemoryPromptStore = InMemoryPromptStore(sessionCount = 1)) = PaywallPrompts(store, navigator) { now }

    @Test
    fun `the taste offer waits for a break after thirty minutes of visuals, once`() {
        val store = InMemoryPromptStore()
        val prompts = prompts(store)
        prompts.onVisualsTime(FreeTier.TASTE_VISUALS_MS - 1_000L, free)
        prompts.onBreak(free)
        assertNull(navigator.request.value)

        prompts.onVisualsTime(1_000L, free)
        assertNull("never mid-song", navigator.request.value)
        prompts.onBreak(free)
        assertEquals(PaywallTrigger.TasteMilestone, navigator.request.value)

        navigator.dismiss()
        prompts.onBreak(free)
        assertNull(navigator.request.value)
    }

    @Test
    fun `visuals time survives the process, saved on leaving`() {
        val store = InMemoryPromptStore()
        prompts(store).apply {
            onVisualsTime(FreeTier.TASTE_VISUALS_MS, free)
            onBackground()
        }
        assertEquals(FreeTier.TASTE_VISUALS_MS, store.visuals)
        prompts(store).onBreak(free)
        assertEquals(PaywallTrigger.TasteMilestone, navigator.request.value)
    }

    @Test
    fun `a return after five minutes opens the paywall, but not in the first session`() {
        val first = prompts(InMemoryPromptStore(sessionCount = 0))
        first.onBackground()
        now += FreeTier.RETURN_AWAY_MS
        first.onForeground(free)
        assertNull("first session", navigator.request.value)

        val later = prompts(InMemoryPromptStore(sessionCount = 1))
        later.onBackground()
        now += FreeTier.RETURN_AWAY_MS - 1
        later.onForeground(free)
        assertNull("too soon", navigator.request.value)

        later.onBackground()
        now += FreeTier.RETURN_AWAY_MS
        later.onForeground(free)
        assertEquals(PaywallTrigger.Returned, navigator.request.value)
    }

    @Test
    fun `no return paywall from a screen the app opened, nor for the trial`() {
        val prompts = prompts()
        prompts.onOutboundLaunch()
        prompts.onBackground()
        now += FreeTier.RETURN_AWAY_MS * 2
        prompts.onForeground(free)
        assertNull(navigator.request.value)

        prompts.onBackground()
        now += FreeTier.RETURN_AWAY_MS * 2
        prompts.onForeground(trial)
        prompts.onVisualsTime(FreeTier.TASTE_VISUALS_MS, trial)
        prompts.onBreak(trial)
        assertNull(navigator.request.value)
    }

    @Test
    fun `an automatic paywall never replaces one the user asked for`() {
        val tapped = PaywallTrigger.Feature(ProFeature.CAST)
        navigator.open(tapped)
        navigator.open(PaywallTrigger.TrialEnded)
        navigator.open(PaywallTrigger.TasteMilestone)
        navigator.open(PaywallTrigger.Returned)
        assertEquals(tapped, navigator.request.value)

        navigator.dismiss()
        navigator.open(PaywallTrigger.Returned)
        navigator.open(PaywallTrigger.TrialEnded)
        assertEquals(PaywallTrigger.TrialEnded, navigator.request.value)
        navigator.open(PaywallTrigger.Settings)
        assertEquals(PaywallTrigger.Settings, navigator.request.value)
    }

    @Test
    fun `a launch that never took the app away does not hide a later return`() {
        val prompts = prompts()
        prompts.onOutboundLaunch()
        prompts.onResumed()
        prompts.onBackground()
        now += FreeTier.RETURN_AWAY_MS
        prompts.onForeground(free)
        assertEquals(PaywallTrigger.Returned, navigator.request.value)
    }

    @Test
    fun `a return counts after the process ended while away`() {
        val store = InMemoryPromptStore(sessionCount = 1)
        prompts(store).onBackground()
        now += FreeTier.RETURN_AWAY_MS
        prompts(store).onForeground(free)
        assertEquals(PaywallTrigger.Returned, navigator.request.value)
    }

    @Test
    fun `the taste offer waits behind a paywall the user opened`() {
        val store = InMemoryPromptStore()
        val prompts = prompts(store)
        navigator.open(PaywallTrigger.Feature(ProFeature.CAST))
        prompts.onVisualsTime(FreeTier.TASTE_VISUALS_MS, free)
        prompts.onBreak(free)
        assertEquals(false, store.shown)
        navigator.dismiss()
        prompts.onBreak(free)
        assertEquals(PaywallTrigger.TasteMilestone, navigator.request.value)
    }

    @Test
    fun `a screen the app opened is still no return after the process ended while there`() {
        val store = InMemoryPromptStore(sessionCount = 1)
        prompts(store).apply {
            onOutboundLaunch()
            onBackground()
        }
        now += FreeTier.RETURN_AWAY_MS * 2
        prompts(store).onForeground(free)
        assertNull(navigator.request.value)
    }
}
