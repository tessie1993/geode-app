package dev.geode.billing

import android.util.Base64
import dev.geode.RingLog
import java.security.KeyFactory
import java.security.PublicKey
import java.security.Signature
import java.security.spec.X509EncodedKeySpec

/**
 * Checks that a purchase really came from Play before Pro is granted.
 *
 * Play signs every purchase's JSON with the app's licence key. The usual place to verify that is a
 * backend; this app has no network and no backend, so it verifies on the device against the
 * public half of the key, which Play Console shows under Monetization setup > Licensing. It is a
 * public key, not a secret, and reaches the build as `PLAY_LICENSE_KEY` (see docs/play/).
 *
 * With no key configured (a fresh checkout, a CI build without the variable) verification is
 * skipped and logged once, rather than refusing every purchase: a store build without the key
 * would otherwise take money and grant nothing.
 */
class PurchaseVerifier(
    base64PublicKey: String,
) {
    private val key: PublicKey? =
        base64PublicKey.takeIf { it.isNotBlank() }?.let { encoded ->
            runCatching { KeyFactory.getInstance(KEY_ALGORITHM).generatePublic(X509EncodedKeySpec(Base64.decode(encoded, Base64.DEFAULT))) }
                .onFailure { RingLog.note(TAG, "licence key does not parse: ${it.javaClass.simpleName}") }
                .getOrNull()
        }

    private var warnedMissing = false

    fun isGenuine(
        signedData: String,
        signature: String,
    ): Boolean {
        val publicKey = key
        if (publicKey == null) {
            if (!warnedMissing) {
                warnedMissing = true
                RingLog.note(TAG, "no PLAY_LICENSE_KEY in this build; purchase signatures are not checked")
            }
            return true
        }
        if (signedData.isEmpty() || signature.isEmpty()) return false
        return runCatching {
            Signature.getInstance(SIGNATURE_ALGORITHM).run {
                initVerify(publicKey)
                update(signedData.toByteArray(Charsets.UTF_8))
                verify(Base64.decode(signature, Base64.DEFAULT))
            }
        }.getOrDefault(false)
    }

    private companion object {
        const val TAG = "PurchaseVerifier"
        const val KEY_ALGORITHM = "RSA"

        // What Play signs purchase data with.
        const val SIGNATURE_ALGORITHM = "SHA1withRSA"
    }
}
