package dev.geode.pro

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import javax.inject.Inject
import javax.inject.Singleton

/** Why the paywall is open, which decides what it leads with. */
sealed interface PaywallTrigger {
    /** A locked feature was tapped; the paywall highlights it. */
    data class Feature(
        val feature: ProFeature,
    ) : PaywallTrigger

    /** First-run setup just finished. Shown once, easy to skip. */
    data object Onboarding : PaywallTrigger

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

        fun open(trigger: PaywallTrigger) {
            _request.value = trigger
        }

        fun dismiss() {
            _request.value = null
        }
    }
