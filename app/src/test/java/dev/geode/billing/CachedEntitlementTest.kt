package dev.geode.billing

import dev.geode.pro.BillingRepository
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
    private val purchase = SignedPurchase(json = """{"productId":"${BillingRepository.PRO_PRODUCT_ID}"}""", signature = "sig")

    /** Stands in for [PurchaseVerifier] with a key configured: only [purchase] verifies. */
    private val keyed: (String, String) -> Boolean = { json, sig -> json == purchase.json && sig == purchase.signature }

    /** [PurchaseVerifier] in a fake-billing build (`allowUnsigned`) passes everything. */
    private val keyless: (String, String) -> Boolean = { _, _ -> true }

    private val owned = CachedEntitlement(owned = true, verifiedAtMs = 1_000L, purchase.json, purchase.signature)

    @Test
    fun `a verified purchase vouches for Pro with no expiry`() {
        assertTrue(owned.vouchesForPro(keyed))
        assertTrue("however long ago Play confirmed it", owned.copy(verifiedAtMs = 0L).vouchesForPro(keyed))
    }

    @Test
    fun `the cache is re-verified on read, so a tampered or forged one does not vouch`() {
        assertFalse(owned.copy(signature = "forged").vouchesForPro(keyed))
        assertFalse(owned.copy(purchaseJson = """{"productId":"other"}""").vouchesForPro(keyed))
        assertFalse(CachedEntitlement.NONE.vouchesForPro(keyless))
        assertFalse(owned.copy(owned = false).vouchesForPro(keyed))
    }

    @Test
    fun `in a fake-billing build the cache is trusted, as the live check is`() {
        assertTrue(CachedEntitlement(owned = true, verifiedAtMs = 0L).vouchesForPro(keyless))
    }

    @Test
    fun `owned without the signed purchase is not Pro, however recent its date`() {
        assertFalse(CachedEntitlement(owned = true, verifiedAtMs = 10_000L).vouchesForPro(keyed))
        assertFalse(CachedEntitlement(owned = true, verifiedAtMs = 10_000L, signature = purchase.signature).vouchesForPro(keyed))
    }

    @Test
    fun `a date in the future does not make an unsigned cache Pro`() {
        assertFalse(CachedEntitlement(owned = true, verifiedAtMs = Long.MAX_VALUE).vouchesForPro(keyed))
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
    fun `one full list without the purchase starts the wait, one a day later revokes`() {
        val grace = CachedEntitlement.REVOKE_GRACE_MS
        val first = owned.after(PlayAnswer.FullList(genuine = null), 99_000L)
        assertTrue("Play's empty cache is not a refund", first.vouchesForPro(keyed))
        assertEquals(99_000L, first.missingSinceMs)

        assertSame(first, first.after(PlayAnswer.FullList(genuine = null), 99_000L + grace - 1))
        assertSame("errors do not run the wait out", first, first.after(PlayAnswer.NoAnswer, 99_000L + grace * 2))

        val after = first.after(PlayAnswer.FullList(genuine = null), 99_000L + grace)
        assertFalse(after.owned)
        assertEquals(99_000L + grace, after.verifiedAtMs)
        assertFalse(after.vouchesForPro(keyless))
    }

    @Test
    fun `the purchase turning up again ends the wait`() {
        val grace = CachedEntitlement.REVOKE_GRACE_MS
        val missing = owned.after(PlayAnswer.FullList(genuine = null), 99_000L)
        val back = missing.after(PlayAnswer.FullList(purchase), 100_000L)
        assertEquals(0L, back.missingSinceMs)
        val again = back.after(PlayAnswer.FullList(genuine = null), 100_000L + grace * 2)
        assertTrue("a later gap waits afresh", again.vouchesForPro(keyed))
    }

    @Test
    fun `a clock moved back restarts the wait and a free cache stays free`() {
        val missing = owned.after(PlayAnswer.FullList(genuine = null), 99_000L)
        assertEquals(50_000L, missing.after(PlayAnswer.FullList(genuine = null), 50_000L).missingSinceMs)
        assertFalse(CachedEntitlement.NONE.after(PlayAnswer.FullList(genuine = null), 5L).owned)
    }

    @Test
    fun `what Play signed must be a completed Pro purchase`() {
        val pro = BillingRepository.PRO_PRODUCT_ID
        val signedAnything: (String, String) -> Boolean = { _, _ -> true }
        val real =
            """{"orderId":"GPA.1","packageName":"com.beatrealms3d.app","productId":"$pro","purchaseTime":1,""" +
                """"purchaseState":0,"purchaseToken":"t","quantity":1,"acknowledged":true}"""
        assertTrue(owned.copy(purchaseJson = real).vouchesForPro(signedAnything))
        val pending = real.replace("\"purchaseState\":0", "\"purchaseState\": 4")
        assertFalse("a pending payment is signed too", owned.copy(purchaseJson = pending).vouchesForPro(signedAnything))
        val other = real.replace(pro, "tip_jar")
        assertFalse("so is any other product", owned.copy(purchaseJson = other).vouchesForPro(signedAnything))

        assertTrue(SignedPurchase.isCompletedPro("""{"productIds":["tip_jar","$pro"],"purchaseState":0}"""))
        assertFalse(SignedPurchase.isCompletedPro("""{"productIds":["tip_jar"]}"""))
        assertFalse(
            "a value that spells the field does not pass for it",
            SignedPurchase.isCompletedPro("""{"purchaseToken":"\"productId\":\"$pro\"","productId":"tip_jar"}"""),
        )
    }

    @Test
    fun `a genuine purchase is stored with its signature, so it can be re-verified offline`() {
        val fromList = CachedEntitlement.NONE.after(PlayAnswer.FullList(purchase), 5_000L)
        assertEquals(CachedEntitlement(owned = true, verifiedAtMs = 5_000L, purchase.json, purchase.signature), fromList)
        assertTrue(fromList.vouchesForPro(keyed))

        val legacy = CachedEntitlement(owned = true, verifiedAtMs = 10_000L)
        assertFalse(legacy.vouchesForPro(keyed))
        val upgraded = legacy.after(PlayAnswer.Update(purchase), 20_000L)
        assertTrue("Play's answer brings the purchase the old cache lacked", upgraded.vouchesForPro(keyed))
    }
}
