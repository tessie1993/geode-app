package dev.geode.billing

import android.content.Context
import android.content.SharedPreferences

/**
 * The last entitlement Play confirmed, kept so Pro survives a slow, missing or offline Play Store.
 *
 * Play's own purchase cache answers offline most of the time, so this is a second line: it is
 * what [dev.geode.pro.BillingRepository.isProStream] starts from before Play has answered, and
 * what it keeps while Play cannot answer at all. A successful query that no longer lists the
 * purchase (a refund, a revoked order) clears it; nothing else does.
 */
data class CachedEntitlement(
    val owned: Boolean,
    val verifiedAtMs: Long,
) {
    /**
     * Whether the cache alone vouches for Pro. It has no expiry: the live wallpaper and the
     * screensaver read only this cache and never ask Play themselves, so a time limit took Pro away
     * from a buyer who simply hadn't opened the app for a while (and the app's first frame after
     * that switched their locked style away before Play could answer). Revocation still works,
     * because every foreground [dev.geode.pro.BillingRepository] refresh that no longer lists the
     * purchase clears [owned]. [verifiedAtMs] is kept for diagnostics.
     */
    fun vouchesForPro(): Boolean = owned

    companion object {
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
