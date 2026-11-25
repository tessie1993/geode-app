package dev.geode.billing

import dev.geode.pro.BillingError
import dev.geode.pro.PurchaseStatus
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.advanceTimeBy
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

@OptIn(ExperimentalCoroutinesApi::class)
class FakeBillingRepositoryTest {
    @Test
    fun `a purchase cancelled mid-flight does not leave the status stuck in progress`() =
        runTest {
            val billing = FakeBillingRepository(latencyMs = 1_000L)
            val attempt = launch { billing.launchPurchase() }
            advanceTimeBy(500L)
            assertEquals(PurchaseStatus.InProgress, billing.getPurchaseStatusStream().first())

            attempt.cancel()
            advanceUntilIdle()
            assertEquals(PurchaseStatus.Idle, billing.getPurchaseStatusStream().first())

            // And the next attempt is not refused.
            launch { billing.launchPurchase() }
            advanceUntilIdle()
            assertEquals(PurchaseStatus.Purchased, billing.getPurchaseStatusStream().first())
            assertTrue(billing.isProStream().first())
        }

    @Test
    fun `with the tester code required, a purchase fails as billing unavailable until the code is entered`() =
        runTest {
            val billing = FakeBillingRepository(requiresTesterCode = true, isTesterCode = { it == "open sesame" })
            billing.launchPurchase()
            assertEquals(PurchaseStatus.Failed(BillingError.BILLING_UNAVAILABLE), billing.getPurchaseStatusStream().first())
            assertFalse(billing.isProStream().first())

            assertFalse(billing.unlockWithCode("not the code"))
            assertFalse(billing.testerUnlocked.value)

            assertTrue(billing.unlockWithCode("open sesame"))
            assertTrue(billing.testerUnlocked.value)
            assertTrue(billing.isProStream().first())
        }

    @Test
    fun `a cached Pro, as the device tests seed it, needs no code`() {
        val store = InMemoryEntitlementStore()
        store.save(CachedEntitlement(owned = true, verifiedAtMs = 1L))
        assertTrue(FakeBillingRepository(store, requiresTesterCode = true).testerUnlocked.value)
    }
}
