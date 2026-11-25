package dev.geode.ui.pro

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import dagger.hilt.android.lifecycle.HiltViewModel
import dev.geode.billing.FakeBillingRepository
import dev.geode.billing.FakePurchaseOutcome
import dev.geode.pro.BillingRepository
import dev.geode.pro.FeatureGate
import dev.geode.pro.GetFeatureGateUseCase
import dev.geode.pro.PaywallNavigator
import dev.geode.pro.PaywallPrompts
import dev.geode.pro.PaywallTrigger
import dev.geode.pro.ProFeature
import dev.geode.pro.ProductLoad
import dev.geode.pro.PurchaseStatus
import dev.geode.pro.RestoreResult
import dev.geode.pro.ShareBonusRepository
import dev.geode.pro.TrialRepository
import dev.geode.pro.TrialState
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.flowOf
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch
import javax.inject.Inject

/** A restore the user asked for, and what it found. */
sealed interface RestoreUiState {
    data object Idle : RestoreUiState

    data object Running : RestoreUiState

    data class Done(
        val result: RestoreResult,
    ) : RestoreUiState
}

/**
 * The fake billing controls, present only when the build bills against [FakeBillingRepository].
 * Until [unlocked] (the tester code was entered) only the code field shows.
 */
data class DebugBillingUiState(
    val owned: Boolean,
    val nextOutcome: FakePurchaseOutcome,
    val hasPending: Boolean,
    val unlocked: Boolean = true,
)

data class PaywallUiState(
    /** Null while the paywall is closed. */
    val trigger: PaywallTrigger? = null,
    val gate: FeatureGate = FeatureGate.FREE,
    val product: ProductLoad = ProductLoad.Loading,
    val purchase: PurchaseStatus = PurchaseStatus.Idle,
    val trial: TrialState = TrialState.Available,
    val restore: RestoreUiState = RestoreUiState.Idle,
    val debug: DebugBillingUiState? = null,
) {
    val visible: Boolean get() = trigger != null

    /** The feature the user tapped, which the comparison table highlights. */
    val highlighted: ProFeature? get() = (trigger as? PaywallTrigger.Feature)?.feature

    val isBusy: Boolean get() = purchase == PurchaseStatus.InProgress || restore == RestoreUiState.Running

    val canPurchase: Boolean
        get() = product is ProductLoad.Ready && !gate.isPro && !isBusy && purchase != PurchaseStatus.Pending

    val canStartTrial: Boolean get() = trial == TrialState.Available && !gate.isPro && !isBusy

    /** The locked style a share would keep, while the once-per-install bonus is unused; else null. */
    val shareBonusStyleId: String?
        get() = (trigger as? PaywallTrigger.Feature)?.styleId?.takeIf { gate.shareBonusAvailable && !gate.allowsStyle(it) }
}

/**
 * The paywall, the Settings "Go Pro" row and the locked-feature panels.
 *
 * Holds no Activity: Play's purchase sheet is launched by the repository from the resumed
 * activity it tracks itself, so this stays a plain, testable state holder.
 */
@HiltViewModel
class PaywallViewModel
    @Inject
    constructor(
        private val billing: BillingRepository,
        private val trial: TrialRepository,
        getFeatureGate: GetFeatureGateUseCase,
        private val navigator: PaywallNavigator,
        private val shareBonus: ShareBonusRepository,
        private val prompts: PaywallPrompts,
    ) : ViewModel() {
        private val restore = MutableStateFlow<RestoreUiState>(RestoreUiState.Idle)
        private val fake = billing as? FakeBillingRepository

        /** For gates outside the paywall itself: the Studio tab, the Settings row. */
        val gate: StateFlow<FeatureGate> = getFeatureGate().stateIn(viewModelScope, SharingStarted.Eagerly, FeatureGate.FREE)

        private val debug: Flow<DebugBillingUiState?> =
            fake?.let { f ->
                combine(f.isProStream(), f.nextOutcome, f.hasPending, f.testerUnlocked) { owned, next, pending, unlocked ->
                    DebugBillingUiState(owned, next, pending, unlocked)
                }
            } ?: flowOf(null)

        private val billingState =
            combine(billing.getProductStream(), billing.getPurchaseStatusStream(), restore) { product, purchase, restoring ->
                Triple(product, purchase, restoring)
            }

        val uiState: StateFlow<PaywallUiState> =
            combine(navigator.request, gate, billingState, trial.getTrialStream(), debug) { trigger, gate, billing, trial, debug ->
                PaywallUiState(
                    trigger = trigger,
                    gate = gate,
                    product = billing.first,
                    purchase = billing.second,
                    trial = trial,
                    restore = billing.third,
                    debug = debug,
                )
            }.stateIn(viewModelScope, SharingStarted.WhileSubscribed(STOP_TIMEOUT_MS), PaywallUiState())

        init {
            viewModelScope.launch { billing.refresh() }
            // "When the trial ends": shown once, the first time the app sees the trial over.
            viewModelScope.launch {
                combine(trial.getTrialStream(), billing.isProStream()) { state, pro -> state to pro }.collect { (state, pro) ->
                    if (state is TrialState.Ended && !state.endNoticeShown) {
                        if (!pro) navigator.open(PaywallTrigger.TrialEnded)
                        trial.markEndNoticeShown()
                    }
                }
            }
        }

        /** The app came to the foreground: pick up purchases completed elsewhere, and maybe a return's paywall. */
        fun onForeground() {
            viewModelScope.launch { billing.refresh() }
            prompts.onForeground(gate.value)
        }

        fun onBackground() = prompts.onBackground()

        /** Visuals on screen, music playing, app in front, for [ms]: counts toward the taste milestone. */
        fun onVisualsTime(ms: Long) = prompts.onVisualsTime(ms, gate.value)

        /** The song ended or changed, or the music paused: where the taste milestone may open. */
        fun onMusicBreak() = prompts.onBreak(gate.value)

        fun open(trigger: PaywallTrigger) {
            navigator.open(trigger)
        }

        fun purchase() {
            if (!uiState.value.canPurchase) return
            restore.value = RestoreUiState.Idle
            viewModelScope.launch { billing.launchPurchase() }
        }

        fun restorePurchases() {
            if (uiState.value.isBusy) return
            billing.clearPurchaseStatus()
            restore.value = RestoreUiState.Running
            viewModelScope.launch { restore.value = RestoreUiState.Done(billing.restorePurchases()) }
        }

        fun retry() {
            billing.clearPurchaseStatus()
            viewModelScope.launch { billing.refresh() }
        }

        fun startTrial() {
            if (!uiState.value.canStartTrial) return
            viewModelScope.launch {
                trial.startTrial()
                dismiss()
            }
        }

        /**
         * The share sheet opened for the app: the style this paywall offers is kept for free, once per
         * install, and the paywall closes on it (the session chooses it as the gate lets it through).
         */
        fun keepStyleForShare() {
            val styleId = uiState.value.shareBonusStyleId ?: return
            if (shareBonus.grant(styleId)) dismiss()
        }

        fun dismiss() {
            billing.clearPurchaseStatus()
            restore.value = RestoreUiState.Idle
            navigator.dismiss()
        }

        /** The tester code typed on the paywall of a debug or sideload build; false when it is wrong. */
        fun enterTesterCode(code: String): Boolean = fake?.unlockWithCode(code) ?: false

        fun setDebugOwned(owned: Boolean) {
            fake?.setOwned(owned)
        }

        fun setDebugOutcome(outcome: FakePurchaseOutcome) {
            fake?.nextOutcome?.value = outcome
        }

        fun completeDebugPending() {
            fake?.completePending()
        }

        private companion object {
            const val STOP_TIMEOUT_MS = 5_000L
        }
    }
