package dev.geode.pro

import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.distinctUntilChanged
import javax.inject.Inject

/**
 * The one entitlement check every gate goes through: owning Pro beats the trial, the trial beats
 * free. Callers ask the resulting [FeatureGate], never billing or the trial directly.
 */
class GetFeatureGateUseCase
    @Inject
    constructor(
        private val billing: BillingRepository,
        private val trial: TrialRepository,
    ) {
        operator fun invoke(): Flow<FeatureGate> =
            combine(billing.isProStream(), trial.getTrialStream()) { pro, trialState ->
                FeatureGate(
                    when {
                        pro -> Entitlement.Pro
                        trialState is TrialState.Active -> Entitlement.Trial(trialState.endsAtMs)
                        else -> Entitlement.Free
                    },
                )
            }.distinctUntilChanged()
    }
