package dev.geode.billing

import dev.geode.BuildConfig
import dev.geode.RingLog
import java.security.KeyFactory
import java.security.PublicKey
import java.security.Signature
import java.security.spec.X509EncodedKeySpec
import java.util.Base64

/**
 * Checks that a purchase really came from Play before Pro is granted.
 *
 * Play signs every purchase's JSON with the app's licence key. The usual place to verify that is a
 * backend; this app has no network and no backend, so it verifies on the device against the
 * public half of the key, which Play Console shows under Monetization setup > Licensing. It is a
 * public key, not a secret, and reaches the build as `PLAY_LICENSE_KEY`.
 *
 * Fails closed: with a blank or unparsable key every purchase is refused, and the reason is logged
 * once. Only a build that bills against [FakeBillingRepository] (`BuildConfig.USE_FAKE_BILLING`)
 * passes [allowUnsigned], because its cache holds no signed purchase to check. The caller says so
 * explicitly; an empty key never means "skip". A usable key is always checked.
 */
class PurchaseVerifier(
    base64PublicKey: String,
    private val allowUnsigned: Boolean = false,
) {
    private val key: PublicKey? =
        base64PublicKey.takeIf { it.isNotBlank() }?.let { encoded ->
            runCatching {
                val spec = X509EncodedKeySpec(Base64.getMimeDecoder().decode(encoded))
                KeyFactory.getInstance(KEY_ALGORITHM).generatePublic(spec)
            }.onFailure { RingLog.note(TAG, "licence key does not parse: ${it.javaClass.simpleName}") }
                .getOrNull()
        }

    private var warnedMissing = false

    fun isGenuine(
        signedData: String,
        signature: String,
    ): Boolean {
        val publicKey = key
        if (publicKey == null) {
            if (allowUnsigned) return true
            if (!warnedMissing) {
                warnedMissing = true
                RingLog.note(TAG, "no usable PLAY_LICENSE_KEY in this build; every purchase is refused")
            }
            return false
        }
        if (signedData.isEmpty() || signature.isEmpty()) return false
        return runCatching {
            Signature.getInstance(SIGNATURE_ALGORITHM).run {
                initVerify(publicKey)
                update(signedData.toByteArray(Charsets.UTF_8))
                verify(Base64.getMimeDecoder().decode(signature))
            }
        }.getOrDefault(false)
    }

    companion object {
        /**
         * The verifier this build's real billing uses: the configured key, unchecked only when the
         * build bills against [FakeBillingRepository], whose cache holds no signed purchase to
         * check. [ProSnapshot] needs the same rule as [PlayBillingRepository] without the injection
         * graph.
         */
        fun forThisBuild(): PurchaseVerifier = PurchaseVerifier(BuildConfig.PLAY_LICENSE_KEY, allowUnsigned = BuildConfig.USE_FAKE_BILLING)

        private const val TAG = "PurchaseVerifier"
        private const val KEY_ALGORITHM = "RSA"

        // What Play signs purchase data with.
        private const val SIGNATURE_ALGORITHM = "SHA1withRSA"
    }
}
