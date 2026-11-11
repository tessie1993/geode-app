package dev.geode.billing

import dev.geode.pro.BillingRepository
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.security.KeyPairGenerator
import java.security.PrivateKey
import java.security.Signature
import java.util.Base64

/**
 * [PurchaseVerifier] fails closed: a real-billing build with a blank or broken licence key trusts
 * no purchase, and only the fake-billing build, which stores no signed purchase, may skip the check.
 */
class PurchaseVerifierTest {
    private val pair = KeyPairGenerator.getInstance("RSA").apply { initialize(2048) }.generateKeyPair()
    private val goodKey = Base64.getEncoder().encodeToString(pair.public.encoded)
    private val data = """{"productId":"${BillingRepository.PRO_PRODUCT_ID}"}"""

    private fun sign(
        text: String,
        privateKey: PrivateKey = pair.private,
    ): String =
        Signature.getInstance("SHA1withRSA").run {
            initSign(privateKey)
            update(text.toByteArray(Charsets.UTF_8))
            Base64.getEncoder().encodeToString(sign())
        }

    @Test
    fun `a blank key trusts no purchase in a real-billing build`() {
        assertFalse(PurchaseVerifier("").isGenuine(data, sign(data)))
        assertFalse(PurchaseVerifier("   ").isGenuine(data, sign(data)))
    }

    @Test
    fun `an unparsable key trusts no purchase`() {
        assertFalse(PurchaseVerifier("not-a-key!!").isGenuine(data, sign(data)))
        assertFalse(PurchaseVerifier(Base64.getEncoder().encodeToString("junk".toByteArray())).isGenuine(data, sign(data)))
    }

    @Test
    fun `a good key accepts the genuine signature and nothing else`() {
        val verifier = PurchaseVerifier(goodKey)
        assertTrue(verifier.isGenuine(data, sign(data)))
        assertFalse(verifier.isGenuine("""{"productId":"other"}""", sign(data)))
        assertFalse(verifier.isGenuine(data, "forged"))
        assertFalse(verifier.isGenuine(data, ""))
        assertFalse(verifier.isGenuine("", sign(data)))
    }

    @Test
    fun `a signature from another key is refused`() {
        val other = KeyPairGenerator.getInstance("RSA").apply { initialize(2048) }.generateKeyPair()
        assertFalse(PurchaseVerifier(goodKey).isGenuine(data, sign(data, other.private)))
    }

    @Test
    fun `the fake-billing build may skip the check, a keyed one still checks`() {
        assertTrue(PurchaseVerifier("", allowUnsigned = true).isGenuine(data, "anything"))
        assertTrue(PurchaseVerifier("not-a-key!!", allowUnsigned = true).isGenuine(data, "anything"))
        assertFalse(PurchaseVerifier(goodKey, allowUnsigned = true).isGenuine(data, "forged"))
    }
}
