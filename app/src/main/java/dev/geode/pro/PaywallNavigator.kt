package dev.geode.pro

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import javax.inject.Inject
import javax.inject.Singleton

/** Why the paywall is open, which decides what it leads with. */
sealed interface PaywallTrigger {
    /**
     * A locked feature was tapped; the paywall highlights it. For a locked style, [styleId] is the
     * style that asked, which a share can keep ([ShareBonusRepository]).
     */
    data class Feature(
        val feature: ProFeature,
        val styleId: String? = null,
    ) : PaywallTrigger

    /** A free user has had a taste of the app ([PaywallPrompts]); shown once, at a break in the music. */
    data object TasteMilestone : PaywallTrigger

    /** A free user came back after a while away ([PaywallPrompts]). */
    data object Returned : PaywallTrigger

    /** Settings' "Go Pro" row. */
    data object Settings : PaywallTrigger

    /** The one-hour trial ran out. */
    data object TrialEnded : PaywallTrigger
}

/**
 * Whether the paywall is showing, as state rather than an event: the host draws it while
 * [request] is non-null, so a rotation or process restore cannot drop or repeat it.
 *
 * App-wide because any surface may open it (the Studio tab, an export dialog, a style picker,
 * onboarding) and exactly one host draws it. When navigation moves to Navigation 3 this becomes a
 * `NavKey` pushed by the app module, and callers keep calling [open].
 */
@Singleton
class PaywallNavigator
    @Inject
    constructor() {
        private val _request = MutableStateFlow<PaywallTrigger?>(null)
        val request: StateFlow<PaywallTrigger?> = _request.asStateFlow()

        /**
         * Shows [trigger] unless a paywall that outranks it is up: what the user tapped (a locked feature,
         * Settings' row) beats the trial's end, which beats the taste milestone, which beats a return. An
         * automatic paywall never replaces the one the user asked for.
         */
        fun open(trigger: PaywallTrigger): Boolean {
            var shown = false
            _request.update { current ->
                shown = current == null || rank(trigger) >= rank(current)
                if (shown) trigger else current
            }
            return shown
        }

        fun dismiss() {
            _request.value = null
        }

        private fun rank(trigger: PaywallTrigger): Int =
            when (trigger) {
                is PaywallTrigger.Feature, PaywallTrigger.Settings -> 3
                PaywallTrigger.TrialEnded -> 2
                PaywallTrigger.TasteMilestone -> 1
                PaywallTrigger.Returned -> 0
            }
    }
