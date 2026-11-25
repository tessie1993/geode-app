package dev.geode.billing

import android.content.SharedPreferences
import dev.geode.data.prefs.PrefType
import dev.geode.data.prefs.prefKeys
import dev.geode.pro.ShareBonusRepository
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow

/** [ShareBonusRepository] in the purchase cache's file, beside the trial's facts (`di/BillingModule`). */
class PrefsShareBonusRepository(
    private val prefs: SharedPreferences,
) : ShareBonusRepository {
    private val style = MutableStateFlow(prefs.getString(KEY_STYLE, null))

    override fun getStyleStream(): Flow<String?> = style.asStateFlow()

    @Synchronized
    override fun grant(styleId: String): Boolean {
        if (style.value != null) return false
        prefs.edit().putString(KEY_STYLE, styleId).apply()
        style.value = styleId
        return true
    }

    internal companion object {
        const val KEY_STYLE = "share_bonus_style"

        val PREF_KEYS =
            prefKeys(PrefsEntitlementStore.PREFS_FILE) {
                state(PrefType.STRING, KEY_STYLE)
            }
    }
}
