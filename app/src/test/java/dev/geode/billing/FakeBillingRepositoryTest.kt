package dev.geode.billing

import dev.geode.pro.PurchaseStatus
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.advanceTimeBy
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
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
}
