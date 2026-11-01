package dev.geode.billing

import android.content.Context
import android.content.SharedPreferences

/**
 * The last entitlement Play confirmed, kept so Pro survives a slow, missing or offline Play Store.
 *
 * Pro is a one-time purchase, so once Play has handed over a purchase whose signature checks out,
 * the cache keeps vouching for it with no expiry. It used to lapse 30 days after the last
 * confirmation, which froze the live wallpaper and the screensaver for buyers who were simply
 * offline (a trip, a tablet that never sees Wi-Fi) and so punished exactly the people who paid.
 *
 * What keeps that from being a hole is [purchaseJson] and [signature]: the purchase as Play signed
 * it. [vouchesForPro] re-checks them against the licence key every time the cache is read, by the
 * same rule as the live check, so a hand-edited prefs file saying `owned` is worth nothing when a
 * key is configured. Pro is taken away only by Play itself: see [after].
 */
data class CachedEntitlement(
    val owned: Boolean,
    val verifiedAtMs: Long,
    /** The purchase's `originalJson` as Play signed it; empty in a cache written before it was kept. */
    val purchaseJson: String = "",
    val signature: String = "",
) {
    /**
     * Whether the cache alone vouches for Pro at [nowMs], with [isGenuine] the licence-key check
     * ([PurchaseVerifier.isGenuine], which passes everything when the build has no key).
     *
     * A cache written by an older version holds `owned` without the purchase. Those still get the
     * old month from their last confirmation ([LEGACY_GRACE_MS]; a clock moved back reads as
     * "just confirmed"), so an update installed offline does not lock anyone out. The next answer
     * from Play rewrites the cache with the purchase, and from then on the month no longer matters.
     */
    fun vouchesForPro(
        nowMs: Long,
        isGenuine: (signedData: String, signature: String) -> Boolean,
    ): Boolean =
        when {
            !owned -> false
            isGenuine(purchaseJson, signature) -> true
            else -> purchaseJson.isEmpty() && nowMs - verifiedAtMs < LEGACY_GRACE_MS
        }

    /**
     * The cache after [answer] from Play at [nowMs]. Only [PlayAnswer.FullList] (a successful
     * purchase query, which lists everything this account holds) may revoke: a refund or a revoked
     * order shows up as the product missing from it, or present but not PURCHASED.
     * [PlayAnswer.NoAnswer] (an error, Play unreachable, no connection) and [PlayAnswer.Update] (a
     * purchase-flow callback, which carries only what changed) never take Pro away.
     */
    fun after(
        answer: PlayAnswer,
        nowMs: Long,
    ): CachedEntitlement =
        when (answer) {
            PlayAnswer.NoAnswer -> this
            is PlayAnswer.Update -> answer.genuine?.let { owning(it, nowMs) } ?: this
            is PlayAnswer.FullList -> answer.genuine?.let { owning(it, nowMs) } ?: CachedEntitlement(owned = false, verifiedAtMs = nowMs)
        }

    private fun owning(
        purchase: SignedPurchase,
        nowMs: Long,
    ): CachedEntitlement = CachedEntitlement(owned = true, verifiedAtMs = nowMs, purchase.json, purchase.signature)

    companion object {
        /** How long a cache from before the purchase was stored outlives Play being unreachable. */
        const val LEGACY_GRACE_MS: Long = 30L * 24L * 60L * 60L * 1000L

        val NONE: CachedEntitlement = CachedEntitlement(owned = false, verifiedAtMs = 0L)
    }
}

/** A completed Pro purchase whose signature already passed [PurchaseVerifier], as Play signed it. */
data class SignedPurchase(
    val json: String,
    val signature: String,
)

/** What one exchange with Play said about the Pro purchase; see [CachedEntitlement.after]. */
sealed interface PlayAnswer {
    /** The connection or the query failed: says nothing about ownership. */
    data object NoAnswer : PlayAnswer

    /** `onPurchasesUpdated`: only the purchases that changed. [genuine] is null when none is a genuine Pro. */
    data class Update(
        val genuine: SignedPurchase?,
    ) : PlayAnswer

    /** A successful `queryPurchasesAsync`: everything Play holds. [genuine] null means Pro is not owned. */
    data class FullList(
        val genuine: SignedPurchase?,
    ) : PlayAnswer
}

/** Storage for [CachedEntitlement]; an interface so the fake and tests can keep it in memory. */
interface EntitlementStore {
    fun load(): CachedEntitlement

    fun save(entitlement: CachedEntitlement)
}

class InMemoryEntitlementStore(
    private var value: CachedEntitlement = CachedEntitlement.NONE,
) : EntitlementStore {
    override fun load(): CachedEntitlement = value

    override fun save(entitlement: CachedEntitlement) {
        value = entitlement
    }
}

/**
 * The on-device cache, in its own small prefs file. Reads are synchronous on purpose: the live
 * wallpaper and the screensaver run outside the UI and its injection graph and must know at once
 * whether to animate (see [ProSnapshot]).
 */
class PrefsEntitlementStore(
    private val prefs: SharedPreferences,
) : EntitlementStore {
    override fun load(): CachedEntitlement =
        CachedEntitlement(
            owned = prefs.getBoolean(KEY_OWNED, false),
            verifiedAtMs = prefs.getLong(KEY_VERIFIED_AT, 0L),
            purchaseJson = prefs.getString(KEY_PURCHASE_JSON, null).orEmpty(),
            signature = prefs.getString(KEY_SIGNATURE, null).orEmpty(),
        )

    override fun save(entitlement: CachedEntitlement) {
        prefs
            .edit()
            .putBoolean(KEY_OWNED, entitlement.owned)
            .putLong(KEY_VERIFIED_AT, entitlement.verifiedAtMs)
            .putString(KEY_PURCHASE_JSON, entitlement.purchaseJson)
            .putString(KEY_SIGNATURE, entitlement.signature)
            .apply()
    }

    companion object {
        const val PREFS_FILE: String = "geode-pro"
        private const val KEY_OWNED = "pro_owned"
        private const val KEY_VERIFIED_AT = "pro_verified_at"
        private const val KEY_PURCHASE_JSON = "pro_purchase_json"
        private const val KEY_SIGNATURE = "pro_purchase_signature"

        fun open(context: Context): SharedPreferences = context.applicationContext.getSharedPreferences(PREFS_FILE, Context.MODE_PRIVATE)
    }
}
