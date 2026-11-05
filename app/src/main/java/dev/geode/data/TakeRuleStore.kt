package dev.geode.data

import android.content.Context
import android.content.SharedPreferences
import androidx.annotation.StringRes
import androidx.core.content.edit
import dev.geode.R
import dev.geode.engine.bridge.GeodeNative
import dev.geode.util.bestEffort
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

/**
 * What pause, seek and a track change do during a take (Settings › Behaviour › Record, #459). The order is the
 * native wire order (`core/output/TakeRules`, `geode_take_set_rule`): the ordinal is what is sent.
 */
enum class TakeRule(
    @StringRes val labelRes: Int,
    @StringRes val detailRes: Int,
) {
    PAUSE_HOLDS_SEEK_CUTS(R.string.take_rule_default, R.string.take_rule_default_detail),
    CONTINUOUS(R.string.take_rule_continuous, R.string.take_rule_continuous_detail),
    PAUSE_HOLDS_SEEK_ENDS(R.string.take_rule_seek_ends, R.string.take_rule_seek_ends_detail),
}

/**
 * The take rule, persisted, and what the engine is told. The decisions are native (`core/output/TakeRules`):
 * this store only keeps the choice and pushes it, like [NoteResponseStore]. A take reads the rule when it
 * begins, so the push only has to land before the next Record; [TakeCapture] fetches the store before every
 * take so a saved choice is always pushed first.
 *
 * [KEY_RULE] lives in `geode-prefs`, which Settings owns in the backup ([dev.geode.data.backup.BackupSection]),
 * so it is backed up, restored and cleared by Reset all settings with the other Settings keys; both restore and
 * reset restart the app, so the cached [instance] reloads the restored or default rule.
 */
class TakeRuleStore internal constructor(
    private val prefs: SharedPreferences,
    private val push: (TakeRule) -> Unit,
) {
    private val _rule =
        MutableStateFlow(prefs.getString(KEY_RULE, null)?.let { name -> TakeRule.entries.firstOrNull { it.name == name } } ?: DEFAULT)

    val rule: StateFlow<TakeRule> = _rule

    init {
        push(_rule.value)
    }

    fun set(rule: TakeRule) {
        _rule.value = rule
        prefs.edit { putString(KEY_RULE, rule.name) }
        push(rule)
    }

    companion object {
        private const val TAG = "TakeRuleStore"
        internal const val KEY_RULE = "take_rule"

        /** The owner's default: pause holds the capture, a seek is a hard cut, a new track ends and saves. */
        val DEFAULT = TakeRule.PAUSE_HOLDS_SEEK_CUTS

        @Volatile private var instance: TakeRuleStore? = null

        fun get(context: Context): TakeRuleStore =
            instance ?: synchronized(this) {
                instance ?: TakeRuleStore(GeodePrefsFiles(context.applicationContext).general) { rule ->
                    bestEffort(TAG, "push take rule") { GeodeNative.takeSetRule(rule.ordinal) }
                }.also { instance = it }
            }
    }
}
