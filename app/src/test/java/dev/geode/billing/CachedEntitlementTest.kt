package dev.geode.billing

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The offline model for Pro: a purchase whose signature verified stays owned with no expiry, the
 * cache is re-verified against the licence key on every read, and only a successful purchase query
 * from Play that no longer lists Pro as purchased takes it away.
 */
class CachedEntitlementTest {
    private val purchase = SignedPurchase(json = """{"productId":"muzviz_pro"}""", signature = "sig")

    /** Stands in for [PurchaseVerifier] with a key configured: only [purchase] verifies. */
    private val keyed: (String, String) -> Boolean = { json, sig -> json == purchase.json && sig == purchase.signature }

    /** [PurchaseVerifier] in a fake-billing build (`allowUnsigned`) passes everything. */
    private val keyless: (String, String) -> Boolean = { _, _ -> true }

    private val owned = CachedEntitlement(owned = true, verifiedAtMs = 1_000L, purchase.json, purchase.signature)

    @Test
    fun `a verified purchase vouches for Pro with no expiry`() {
        assertTrue(owned.vouchesForPro(1_000L, keyed))
        assertTrue(owned.vouchesForPro(1_000L + CachedEntitlement.LEGACY_GRACE_MS * 24, keyed))
        assertTrue("a clock moved back changes nothing", owned.vouchesForPro(0L, keyed))
    }

    @Test
    fun `the cache is re-verified on read, so a tampered or forged one does not vouch`() {
        assertFalse(owned.copy(signature = "forged").vouchesForPro(1_000L, keyed))
        assertFalse(owned.copy(purchaseJson = """{"productId":"other"}""").vouchesForPro(1_000L, keyed))
        assertFalse(CachedEntitlement.NONE.vouchesForPro(0L, keyless))
        assertFalse(owned.copy(owned = false).vouchesForPro(1_000L, keyed))
    }

    @Test
    fun `in a fake-billing build the cache is trusted, as the live check is`() {
        assertTrue(CachedEntitlement(owned = true, verifiedAtMs = 0L).vouchesForPro(Long.MAX_VALUE, keyless))
    }

    @Test
    fun `a cache from before the purchase was stored keeps the old month, then needs Play`() {
        val legacy = CachedEntitlement(owned = true, verifiedAtMs = 10_000L)
        assertTrue(legacy.vouchesForPro(10_000L, keyed))
        assertTrue(legacy.vouchesForPro(10_000L + CachedEntitlement.LEGACY_GRACE_MS - 1, keyed))
        assertFalse(legacy.vouchesForPro(10_000L + CachedEntitlement.LEGACY_GRACE_MS, keyed))
        assertTrue("a clock moved back reads as just confirmed", legacy.vouchesForPro(0L, keyed))
    }

    @Test
    fun `no answer from Play never revokes`() {
        assertSame(owned, owned.after(PlayAnswer.NoAnswer, 99_000L))
        val legacy = CachedEntitlement(owned = true, verifiedAtMs = 10_000L)
        assertSame(legacy, legacy.after(PlayAnswer.NoAnswer, 99_000L))
    }

    @Test
    fun `a purchase-flow update without Pro never revokes`() {
        assertSame(owned, owned.after(PlayAnswer.Update(genuine = null), 99_000L))
    }

    @Test
    fun `a full list without a genuine purchase revokes`() {
        val after = owned.after(PlayAnswer.FullList(genuine = null), 99_000L)
        assertFalse(after.owned)
        assertEquals(99_000L, after.verifiedAtMs)
        assertFalse(after.vouchesForPro(99_000L, keyless))
    }

    @Test
    fun `a genuine purchase is stored with its signature, so it can be re-verified offline`() {
        val fromList = CachedEntitlement.NONE.after(PlayAnswer.FullList(purchase), 5_000L)
        assertEquals(CachedEntitlement(owned = true, verifiedAtMs = 5_000L, purchase.json, purchase.signature), fromList)
        assertTrue(fromList.vouchesForPro(Long.MAX_VALUE, keyed))

        val legacy = CachedEntitlement(owned = true, verifiedAtMs = 10_000L)
        val upgraded = legacy.after(PlayAnswer.Update(purchase), 20_000L)
        assertTrue("past the legacy month, the stored purchase carries it", upgraded.vouchesForPro(Long.MAX_VALUE, keyed))
    }
}
