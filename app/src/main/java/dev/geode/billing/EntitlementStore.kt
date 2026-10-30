package dev.geode.billing

import android.content.Context
import android.content.SharedPreferences

/**
 * The last entitlement Play confirmed, kept so Pro survives a slow, missing or offline Play Store.
 *
 * Play's own purchase cache answers offline most of the time, so this is a second line: it is
 * what [dev.geode.pro.BillingRepository.isProStream] starts from before Play has answered, and
 * what it keeps while Play cannot answer at all, for up to [GRACE_MS] after the last confirmation.
 * A successful query that no longer lists the purchase (a refund, a revoked order) clears it.
 */
data class CachedEntitlement(
    val owned: Boolean,
    val verifiedAtMs: Long,
) {
    /**
     * Whether the cache alone still vouches for Pro at [nowMs]. A clock earlier than the last
     * confirmation reads as "just confirmed" rather than as expired: moving the clock back must not
     * take away something the user paid for.
     */
    fun vouchesForPro(nowMs: Long): Boolean = owned && nowMs - verifiedAtMs < GRACE_MS

    companion object {
        /** How long Pro outlives Play being unreachable. A month covers any trip or outage. */
        const val GRACE_MS: Long = 30L * 24L * 60L * 60L * 1000L

        val NONE: CachedEntitlement = CachedEntitlement(owned = false, verifiedAtMs = 0L)
    }
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
        )

    override fun save(entitlement: CachedEntitlement) {
        prefs
            .edit()
            .putBoolean(KEY_OWNED, entitlement.owned)
            .putLong(KEY_VERIFIED_AT, entitlement.verifiedAtMs)
            .apply()
    }

    companion object {
        const val PREFS_FILE: String = "geode-pro"
        private const val KEY_OWNED = "pro_owned"
        private const val KEY_VERIFIED_AT = "pro_verified_at"

        fun open(context: Context): SharedPreferences = context.applicationContext.getSharedPreferences(PREFS_FILE, Context.MODE_PRIVATE)
    }
}
