package dev.geode.billing

import com.android.billingclient.api.BillingClient.BillingResponseCode
import dev.geode.pro.BillingError
import dev.geode.pro.PurchaseStatus
import org.junit.Assert.assertEquals
import org.junit.Test

/** The decisions [PlayBillingRepository] makes on Play's answers, apart from the client itself. */
class PlayBillingRulesTest {
    private val failed = PurchaseStatus.Failed(BillingError.UNKNOWN)

    private fun PurchaseStatus.afterUpdate(
        completed: Boolean = false,
        pending: Boolean = false,
        anyPro: Boolean = completed || pending,
    ) = after(completed, pending, anyPro, fullList = false)

    private fun PurchaseStatus.afterQuery(
        completed: Boolean = false,
        pending: Boolean = false,
    ) = after(completed, pending, anyPro = completed || pending, fullList = true)

    @Test
    fun `a purchase flow that ends with nothing to grant does not stay in progress`() {
        val open = PurchaseStatus.InProgress.afterUpdate()
        assertEquals("an OK result with no purchases waits for the query", PurchaseStatus.InProgress, open)
        assertEquals("and fails when Play lists no Pro", failed, open.settled(isPro = false))
        assertEquals("or completes when it does (a code redeemed in the sheet)", PurchaseStatus.Purchased, open.settled(isPro = true))
        assertEquals("a purchase that failed verification", failed, PurchaseStatus.InProgress.afterUpdate(anyPro = true))
        assertEquals("the same outside a flow", failed, PurchaseStatus.Idle.afterUpdate(anyPro = true))
    }

    @Test
    fun `settling leaves what the query already decided`() {
        assertEquals("already owned, payment pending", PurchaseStatus.Pending, PurchaseStatus.Pending.settled(isPro = false))
        assertEquals(PurchaseStatus.Purchased, PurchaseStatus.Purchased.settled(isPro = true))
        assertEquals(PurchaseStatus.Idle, PurchaseStatus.Idle.settled(isPro = true))
    }

    @Test
    fun `a completed purchase finishes the attempt it belongs to`() {
        assertEquals(PurchaseStatus.Purchased, PurchaseStatus.InProgress.afterUpdate(completed = true))
        assertEquals(PurchaseStatus.Purchased, PurchaseStatus.Pending.afterQuery(completed = true))
        assertEquals("a routine refresh of an owner says nothing", PurchaseStatus.Idle, PurchaseStatus.Idle.afterQuery(completed = true))
    }

    @Test
    fun `a pending payment waits, and a query that no longer lists it stops waiting`() {
        assertEquals(PurchaseStatus.Pending, PurchaseStatus.InProgress.afterUpdate(pending = true))
        assertEquals(PurchaseStatus.Pending, PurchaseStatus.Idle.afterQuery(pending = true))
        assertEquals(PurchaseStatus.Idle, PurchaseStatus.Pending.afterQuery())
    }

    @Test
    fun `a purchase query never ends an attempt whose sheet may still be up`() {
        assertEquals(PurchaseStatus.InProgress, PurchaseStatus.InProgress.afterQuery())
    }

    @Test
    fun `a service that drops during setup answers instead of leaving the caller waiting`() {
        val results = mutableListOf<Int>()
        setupListener(results::add).onBillingServiceDisconnected()
        assertEquals(listOf(BillingResponseCode.SERVICE_DISCONNECTED), results)
    }

    @Test
    fun `response codes map to the paywall's wording`() {
        assertEquals(BillingError.BILLING_UNAVAILABLE, billingErrorFor(BillingResponseCode.BILLING_UNAVAILABLE))
        assertEquals(BillingError.NETWORK, billingErrorFor(BillingResponseCode.SERVICE_DISCONNECTED))
        assertEquals(BillingError.NETWORK, billingErrorFor(BillingResponseCode.NETWORK_ERROR))
        assertEquals(BillingError.PRODUCT_UNAVAILABLE, billingErrorFor(BillingResponseCode.ITEM_UNAVAILABLE))
        assertEquals("not a country restriction", BillingError.UNKNOWN, billingErrorFor(BillingResponseCode.DEVELOPER_ERROR))
        assertEquals(BillingError.UNKNOWN, billingErrorFor(BillingResponseCode.ERROR))
    }
}
