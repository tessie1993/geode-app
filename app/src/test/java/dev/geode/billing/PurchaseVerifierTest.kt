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
 * A signed purchase must also name this install's package.
 */
class PurchaseVerifierTest {
    private val pair = KeyPairGenerator.getInstance("RSA").apply { initialize(2048) }.generateKeyPair()
    private val goodKey = Base64.getEncoder().encodeToString(pair.public.encoded)
    private val pkg = "com.beatrealms3d.app"
    private val data = """{"packageName":"$pkg","productId":"${BillingRepository.PRO_PRODUCT_ID}"}"""

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
        assertFalse(PurchaseVerifier("", pkg).isGenuine(data, sign(data)))
        assertFalse(PurchaseVerifier("   ", pkg).isGenuine(data, sign(data)))
    }

    @Test
    fun `an unparsable key trusts no purchase`() {
        assertFalse(PurchaseVerifier("not-a-key!!", pkg).isGenuine(data, sign(data)))
        assertFalse(PurchaseVerifier(Base64.getEncoder().encodeToString("junk".toByteArray()), pkg).isGenuine(data, sign(data)))
    }

    @Test
    fun `a good key accepts the genuine signature and nothing else`() {
        val verifier = PurchaseVerifier(goodKey, pkg)
        assertTrue(verifier.isGenuine(data, sign(data)))
        assertFalse(verifier.isGenuine("""{"packageName":"$pkg","productId":"other"}""", sign(data)))
        assertFalse(verifier.isGenuine(data, "forged"))
        assertFalse(verifier.isGenuine(data, ""))
        assertFalse(verifier.isGenuine("", sign(data)))
    }

    @Test
    fun `a signature from another key is refused`() {
        val other = KeyPairGenerator.getInstance("RSA").apply { initialize(2048) }.generateKeyPair()
        assertFalse(PurchaseVerifier(goodKey, pkg).isGenuine(data, sign(data, other.private)))
    }

    @Test
    fun `a signed purchase must name this install's package`() {
        val other = """{"packageName":"com.example.other","productId":"${BillingRepository.PRO_PRODUCT_ID}"}"""
        assertFalse("another app's purchase", PurchaseVerifier(goodKey, pkg).isGenuine(other, sign(other)))
        val none = """{"productId":"${BillingRepository.PRO_PRODUCT_ID}"}"""
        assertFalse("no package at all", PurchaseVerifier(goodKey, pkg).isGenuine(none, sign(none)))
        assertFalse("the release's purchase in the debug build", PurchaseVerifier(goodKey, "$pkg.debug").isGenuine(data, sign(data)))
        val debug = data.replace(pkg, "$pkg.debug")
        assertTrue("the debug build's own", PurchaseVerifier(goodKey, "$pkg.debug").isGenuine(debug, sign(debug)))
        val spelled = """{"orderId":"\"packageName\":\"$pkg\"","packageName":"com.example.other"}"""
        assertFalse("a value that spells the field does not pass for it", PurchaseVerifier(goodKey, pkg).isGenuine(spelled, sign(spelled)))
    }

    @Test
    fun `the fake-billing build may skip the check, a keyed one still checks`() {
        assertTrue(PurchaseVerifier("", pkg, allowUnsigned = true).isGenuine(data, "anything"))
        assertTrue(PurchaseVerifier("not-a-key!!", pkg, allowUnsigned = true).isGenuine(data, "anything"))
        assertFalse(PurchaseVerifier(goodKey, pkg, allowUnsigned = true).isGenuine(data, "forged"))
    }
}
