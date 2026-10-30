package dev.geode.ui.pro

import dev.geode.billing.FakeBillingRepository
import dev.geode.billing.FakePurchaseOutcome
import dev.geode.billing.StoredTrialRepository
import dev.geode.billing.TrialStore
import dev.geode.billing.WallClock
import dev.geode.pro.BillingError
import dev.geode.pro.Entitlement
import dev.geode.pro.FreeTier
import dev.geode.pro.GetFeatureGateUseCase
import dev.geode.pro.PaywallNavigator
import dev.geode.pro.PaywallTrigger
import dev.geode.pro.ProFeature
import dev.geode.pro.ProductLoad
import dev.geode.pro.PurchaseStatus
import dev.geode.pro.RestoreResult
import dev.geode.pro.TrialState
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.UnconfinedTestDispatcher
import kotlinx.coroutines.test.advanceTimeBy
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

@OptIn(ExperimentalCoroutinesApi::class)
class PaywallViewModelTest {
    private val dispatcher = StandardTestDispatcher()

    @Before
    fun setUp() {
        Dispatchers.setMain(dispatcher)
    }

    @After
    fun tearDown() {
        Dispatchers.resetMain()
    }

    private class InMemoryTrialStore : TrialStore {
        var startedAt: Long? = null
        var noticeShown = false

        override fun startedAtMs(): Long? = startedAt

        override fun endNoticeShown(): Boolean = noticeShown

        override fun saveStartedAt(ms: Long) {
            startedAt = ms
        }

        override fun saveEndNoticeShown() {
            noticeShown = true
        }
    }

    private class Fixture(
        scope: TestScope,
        product: ProductLoad = ProductLoad.Ready(FakeBillingRepository.SAMPLE_PRODUCT),
    ) {
        val clock = WallClock { scope.testScheduler.currentTime }
        val billing = FakeBillingRepository(clock = clock, product = product)
        val trialStore = InMemoryTrialStore()
        val trial = StoredTrialRepository(trialStore, clock)
        val navigator = PaywallNavigator()
        val viewModel = PaywallViewModel(billing, trial, GetFeatureGateUseCase(billing, trial), navigator)

        init {
            // uiState is WhileSubscribed, as the screen collects it; keep a subscriber for the test.
            scope.backgroundScope.launch(UnconfinedTestDispatcher(scope.testScheduler)) { viewModel.uiState.collect {} }
        }

        val state get() = viewModel.uiState.value
    }

    @Test
    fun `price comes from the product once billing has refreshed`() =
        runTest(dispatcher) {
            val f = Fixture(this)
            assertEquals(ProductLoad.Loading, f.state.product)
            assertFalse(f.state.canPurchase)

            advanceUntilIdle()

            assertEquals(ProductLoad.Ready(FakeBillingRepository.SAMPLE_PRODUCT), f.state.product)
            assertTrue(f.state.canPurchase)
        }

    @Test
    fun `opening from a locked feature highlights that feature`() =
        runTest(dispatcher) {
            val f = Fixture(this)
            f.viewModel.open(PaywallTrigger.Feature(ProFeature.EXPORT_4K))
            advanceUntilIdle()

            assertTrue(f.state.visible)
            assertEquals(ProFeature.EXPORT_4K, f.state.highlighted)
        }

    @Test
    fun `a successful purchase unlocks Pro`() =
        runTest(dispatcher) {
            val f = Fixture(this)
            f.viewModel.open(PaywallTrigger.Settings)
            advanceUntilIdle()

            f.viewModel.purchase()
            advanceUntilIdle()

            assertEquals(PurchaseStatus.Purchased, f.state.purchase)
            assertEquals(Entitlement.Pro, f.state.gate.entitlement)
            assertTrue(f.viewModel.gate.value.allows(ProFeature.STUDIO))
            assertFalse(f.state.canPurchase)
        }

    @Test
    fun `a pending payment does not unlock Pro until it completes`() =
        runTest(dispatcher) {
            val f = Fixture(this)
            advanceUntilIdle()
            f.billing.nextOutcome.value = FakePurchaseOutcome.PENDING

            f.viewModel.purchase()
            advanceUntilIdle()

            assertEquals(PurchaseStatus.Pending, f.state.purchase)
            assertFalse(f.state.gate.isPro)
            assertFalse("no second purchase while one is pending", f.state.canPurchase)

            f.billing.completePending()
            advanceUntilIdle()

            assertEquals(PurchaseStatus.Purchased, f.state.purchase)
            assertTrue(f.state.gate.isPro)
        }

    @Test
    fun `a failed purchase reports the error and can be retried`() =
        runTest(dispatcher) {
            val f = Fixture(this)
            advanceUntilIdle()
            f.billing.nextOutcome.value = FakePurchaseOutcome.FAIL_NETWORK

            f.viewModel.purchase()
            advanceUntilIdle()

            assertEquals(PurchaseStatus.Failed(BillingError.NETWORK), f.state.purchase)
            assertFalse(f.state.gate.isPro)
            assertTrue(f.state.canPurchase)

            f.billing.nextOutcome.value = FakePurchaseOutcome.SUCCEED
            f.viewModel.purchase()
            advanceUntilIdle()
            assertTrue(f.state.gate.isPro)
        }

    @Test
    fun `cancelling leaves the user free and dismiss clears the status`() =
        runTest(dispatcher) {
            val f = Fixture(this)
            f.viewModel.open(PaywallTrigger.Onboarding)
            advanceUntilIdle()
            f.billing.nextOutcome.value = FakePurchaseOutcome.CANCEL

            f.viewModel.purchase()
            advanceUntilIdle()
            assertEquals(PurchaseStatus.Cancelled, f.state.purchase)

            f.viewModel.dismiss()
            advanceUntilIdle()
            assertEquals(PurchaseStatus.Idle, f.state.purchase)
            assertNull(f.state.trigger)
            assertFalse(f.state.visible)
        }

    @Test
    fun `restore reports nothing for a free user and restored for an owner`() =
        runTest(dispatcher) {
            val f = Fixture(this)
            advanceUntilIdle()

            f.viewModel.restorePurchases()
            assertEquals(RestoreUiState.Running, f.state.restore)
            advanceUntilIdle()
            assertEquals(RestoreUiState.Done(RestoreResult.NothingToRestore), f.state.restore)

            f.billing.setOwned(true)
            f.viewModel.restorePurchases()
            advanceUntilIdle()
            assertEquals(RestoreUiState.Done(RestoreResult.Restored), f.state.restore)
            assertTrue(f.state.gate.isPro)
        }

    @Test
    fun `an unavailable product disables purchase but not the trial`() =
        runTest(dispatcher) {
            val f = Fixture(this, product = ProductLoad.Unavailable(BillingError.BILLING_UNAVAILABLE))
            advanceUntilIdle()

            assertEquals(ProductLoad.Unavailable(BillingError.BILLING_UNAVAILABLE), f.state.product)
            assertFalse(f.state.canPurchase)
            assertTrue(f.state.canStartTrial)
        }

    @Test
    fun `the trial unlocks for an hour, keeps the watermark, then shows the trial-ended paywall once`() =
        runTest(dispatcher) {
            val f = Fixture(this)
            f.viewModel.open(PaywallTrigger.Settings)
            advanceUntilIdle()

            f.viewModel.startTrial()
            runCurrent()

            val entitlement = f.state.gate.entitlement
            assertTrue(entitlement is Entitlement.Trial)
            assertTrue(f.state.gate.allows(ProFeature.EXPORT_4K))
            assertTrue(f.state.gate.allowsStyle("any-premium-style"))
            assertEquals(!FreeTier.TRIAL_REMOVES_WATERMARK, f.state.gate.watermarksExports)
            assertNull("starting the trial closes the paywall", f.state.trigger)
            assertFalse("the trial runs once", f.state.canStartTrial)

            advanceTimeBy(FreeTier.TRIAL_DURATION_MS + 1)
            runCurrent()

            assertEquals(Entitlement.Free, f.state.gate.entitlement)
            assertEquals(PaywallTrigger.TrialEnded, f.state.trigger)
            assertEquals(TrialState.Ended(endNoticeShown = true), f.state.trial)

            f.viewModel.dismiss()
            advanceUntilIdle()
            assertNull("the trial-ended paywall shows once", f.state.trigger)
        }

    @Test
    fun `a Pro owner whose trial ended is not shown the trial-ended paywall`() =
        runTest(dispatcher) {
            val f = Fixture(this)
            f.trialStore.startedAt = 0L
            f.billing.setOwned(true)
            advanceTimeBy(FreeTier.TRIAL_DURATION_MS * 2)
            val fresh = PaywallViewModel(f.billing, StoredTrialRepository(f.trialStore, f.clock), GetFeatureGateUseCase(f.billing, f.trial), f.navigator)
            advanceUntilIdle()

            assertNull(f.navigator.request.value)
            assertTrue(fresh.gate.value.isPro)
        }

    @Test
    fun `the debug panel appears only with fake billing and drives it`() =
        runTest(dispatcher) {
            val f = Fixture(this)
            advanceUntilIdle()
            assertFalse(f.state.debug!!.owned)

            f.viewModel.setDebugOwned(true)
            advanceUntilIdle()
            assertTrue(f.state.debug!!.owned)
            assertTrue(f.state.gate.isPro)

            f.viewModel.setDebugOutcome(FakePurchaseOutcome.PENDING)
            advanceUntilIdle()
            assertEquals(FakePurchaseOutcome.PENDING, f.state.debug!!.nextOutcome)
        }
}
