package dev.geode.billing

import java.security.MessageDigest

/**
 * The code that unlocks Pro in a build billed against [FakeBillingRepository] (debug and sideload,
 * never `release`: [BillingBuild.usesFake]). Only its SHA-256 is kept here; the owner hands the code
 * to testers. It is a speed bump against a casual sideload user, not a secret: the Play build never
 * reads it.
 */
object TesterCode {
    private const val SHA256_HEX = "ed7951d7973b47cd36f08364fa2ecaf5ac9627cc6a990223d03c69cf73b69dd0"

    /** Whether [input] is the code: case and surrounding spaces are ignored. */
    fun matches(input: String): Boolean {
        val digest = MessageDigest.getInstance("SHA-256").digest(input.trim().lowercase().toByteArray(Charsets.UTF_8))
        val hex = digest.joinToString("") { "%02x".format(it) }
        return MessageDigest.isEqual(hex.toByteArray(), SHA256_HEX.toByteArray())
    }
}
