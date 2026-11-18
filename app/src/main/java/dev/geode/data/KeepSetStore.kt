package dev.geode.data

import android.content.SharedPreferences
import dev.geode.data.prefs.PrefType
import dev.geode.data.prefs.prefKeys
import dev.geode.engine.bridge.CustomizeNative
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

/**
 * Customize's keep set: the controls and `#CATEGORY` markers a roll leaves alone, which the director's
 * reach never moves either. Saved in `geode-mod` as a string set under [KEY], one kept key an entry,
 * and handled everywhere else as native's keep document, one key a line. What the document may hold
 * and what a toggle does are native (core/viz/CustomizeMap `keepNormalize`, `keepToggle`); this store
 * reads the file, writes what it is given and holds the latest for the screen and the rolls.
 *
 * One instance per process, built by `GeodeContainer`. The file is read on first use, which the
 * auto-director's start makes on IO.
 */
class KeepSetStore(
    private val prefs: SharedPreferences,
    private val normalize: (String) -> String = { CustomizeNative.customizeKeepNormalize(it) },
) {
    private val saved by lazy {
        val text = prefs.getStringSet(KEY, emptySet()).orEmpty().joinToString("\n")
        val document = runCatching { normalize(text) }.getOrDefault(text)
        MutableStateFlow(document) to MutableStateFlow(keysOf(document))
    }

    /** The saved keep document as native normalised it: one key a line. */
    val document: StateFlow<String> get() = saved.first

    /** The keys [document] keeps, for the panel's kept marks. */
    val keys: StateFlow<Set<String>> get() = saved.second

    /** Saves [document], a keep document native wrote (a toggle), and publishes it. */
    fun save(document: String) {
        val keys = keysOf(document)
        saved.first.value = document
        saved.second.value = keys
        prefs.edit().putStringSet(KEY, keys).apply()
    }

    private fun keysOf(document: String): Set<String> = document.lineSequence().filter { it.isNotEmpty() }.toSet()

    companion object {
        const val KEY = "locked_params"

        val PREF_KEYS =
            prefKeys(GeodePrefsFiles.MODULATION) {
                portable(PrefType.STRING_SET, KEY)
            }
    }
}
