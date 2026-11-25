package dev.geode.billing

import android.content.SharedPreferences
import dev.geode.data.prefs.PrefType
import dev.geode.data.prefs.prefKeys
import dev.geode.pro.PromptStore

/** [PromptStore] in the purchase cache's file, beside the trial's facts (`di/BillingModule`). */
class PrefsPromptStore(
    private val prefs: SharedPreferences,
) : PromptStore {
    override fun sessions(): Int = prefs.getInt(KEY_SESSIONS, 0)

    override fun saveSessions(count: Int) {
        prefs.edit().putInt(KEY_SESSIONS, count).apply()
    }

    override fun visualsMs(): Long = prefs.getLong(KEY_VISUALS_MS, 0L)

    override fun saveVisualsMs(ms: Long) {
        prefs.edit().putLong(KEY_VISUALS_MS, ms).apply()
    }

    override fun milestoneShown(): Boolean = prefs.getBoolean(KEY_MILESTONE_SHOWN, false)

    override fun saveMilestoneShown() {
        prefs.edit().putBoolean(KEY_MILESTONE_SHOWN, true).apply()
    }

    override fun stoppedAtMs(): Long = prefs.getLong(KEY_STOPPED_AT, 0L)

    override fun saveStoppedAt(ms: Long) {
        prefs.edit().putLong(KEY_STOPPED_AT, ms).apply()
    }

    internal companion object {
        const val KEY_SESSIONS = "prompt_sessions"
        const val KEY_VISUALS_MS = "prompt_visuals_ms"
        const val KEY_MILESTONE_SHOWN = "prompt_milestone_shown"
        const val KEY_STOPPED_AT = "prompt_stopped_at"

        val PREF_KEYS =
            prefKeys(PrefsEntitlementStore.PREFS_FILE) {
                state(PrefType.INT, KEY_SESSIONS)
                state(PrefType.LONG, KEY_VISUALS_MS, KEY_STOPPED_AT)
                state(PrefType.BOOLEAN, KEY_MILESTONE_SHOWN)
            }
    }
}
