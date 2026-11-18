package dev.geode.data

import android.content.SharedPreferences
import dev.geode.data.prefs.PrefType
import dev.geode.data.prefs.prefKeys
import dev.geode.engine.bridge.ModulationNative
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

/**
 * Customize's keep set: the controls and `#CATEGORY` markers a roll leaves alone, which the director's
 * reach never moves either. Saved in `geode-mod` as a string set under [KEY], one kept key an entry.
 * Native holds the set while the app runs (core/viz/ModState): it normalises what was saved, applies each
 * Keep tap and hands the director every new set. This store reads the file once into native, and saves and
 * shows what native holds after each tap; it decides nothing.
 *
 * One instance per process, built by `GeodeContainer`. The file is read on first use, which the
 * auto-director's start makes on IO. [load], [toggle] and [held] are native's; seams for JVM tests.
 */
class KeepSetStore(
    private val prefs: SharedPreferences,
    private val load: (String) -> Unit = ModulationNative::modStateKeepLoad,
    private val toggle: (String) -> Unit = ModulationNative::modStateKeepToggle,
    private val held: () -> String? = { ModulationNative.modStateText(ModulationNative.MOD_STATE_KEEP) },
) {
    private val saved by lazy {
        val text = prefs.getStringSet(KEY, emptySet()).orEmpty().joinToString("\n")
        val document = runCatching { load(text) }.mapCatching { held() }.getOrNull() ?: text
        MutableStateFlow(document) to MutableStateFlow(keysOf(document))
    }

    /** The keep document native holds: one key a line. */
    val document: StateFlow<String> get() = saved.first

    /** The keys [document] keeps, for the panel's kept marks. */
    val keys: StateFlow<Set<String>> get() = saved.second

    /** Reads the saved set into native, once; the director has it from then on. */
    fun start() {
        saved
    }

    /** A Keep tap on [key]: native toggles it in the set it holds; this saves and publishes what it holds now. */
    fun tap(key: String) {
        start()
        val document = runCatching { toggle(key) }.mapCatching { held() }.getOrNull() ?: return
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
