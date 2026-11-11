package dev.geode.data

import android.content.Context
import android.content.SharedPreferences
import dev.geode.RingLog
import dev.geode.engine.bridge.GeodeNative
import dev.geode.util.bestEffort
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

/** One knob of the macro bank as the shell draws it, read off the bank text; [value] runs -1..1. */
data class MacroKnob(
    val id: Int,
    val name: String,
    val hint: String,
    val value: Float,
)

/** The bank as stored ([text], opaque to Kotlin) and the knobs its `knob|` rows list, in bank order. */
data class MacroBankState(
    val text: String = "",
    val knobs: List<MacroKnob> = emptyList(),
)

/**
 * Quick knobs' storage, process-wide: the macro bank (core/viz/MacroBank) as the one string native
 * writes. Every edit (a turn, a name, a link, a new or removed knob) is a native call that returns the
 * new bank; this keeps the text, reads the knobs off it for drawing, hands it to the engine at once and
 * saves it, so the knob editor has no Save. [GeodeApp] builds the store at start, so a
 * wallpaper or dream that starts before the app does still gets the bank.
 *
 * The six old knobs, their locks and saved sets are gone with no migration: their keys are dropped
 * the first time the store loads ([RETIRED_KEYS]).
 */
class MacroStore internal constructor(
    private val prefs: SharedPreferences,
    private val normalize: (String) -> String = GeodeNative::macroBankNormalize,
    private val push: (String) -> Unit = { text -> bestEffort(TAG, "push macro bank") { GeodeNative.macroBankSet(text) } },
) {
    private val _state = MutableStateFlow(load())
    val state: StateFlow<MacroBankState> = _state

    init {
        push(_state.value.text)
    }

    /** What the knob editor draws and offers on [styleId], as native lists it for the bank as it is now. */
    fun editor(styleId: String): MacroEditorState =
        runCatching { MacroEditorTable.parse(GeodeNative.macroBankEditor(_state.value.text, styleId)) }
            .onFailure { RingLog.note(TAG, "macro editor read failed", it) }
            .getOrDefault(MacroEditorState())

    fun rename(
        id: Int,
        name: String,
    ) = edit { GeodeNative.macroBankRename(it, id, name) }

    /** The new knob's id, or null when the bank is full. */
    fun addKnob(): Int? = added { GeodeNative.macroBankAddKnob(it, "") }

    /** The copy's id, or null when the bank is full. */
    fun duplicate(id: Int): Int? = added { GeodeNative.macroBankDuplicateKnob(it, id) }

    fun remove(id: Int) = edit { GeodeNative.macroBankRemoveKnob(it, id) }

    fun move(
        id: Int,
        delta: Int,
    ) = edit { GeodeNative.macroBankMoveKnob(it, id, delta) }

    /** A built-in knob's name and links as built. */
    fun resetKnob(id: Int) = edit { GeodeNative.macroBankResetKnob(it, id) }

    /** The four built-in knobs alone, as built and centred. */
    fun resetAll() = edit { GeodeNative.macroBankDefault() }

    fun addLink(
        id: Int,
        target: String,
    ) = edit { GeodeNative.macroBankAddLink(it, id, target) }

    /** A drag moves the link live; [commit] once the gesture ends, like [setValue]. */
    fun setLink(
        id: Int,
        target: String,
        up: Float,
        down: Float,
        persist: Boolean = true,
    ) = edit(persist) { GeodeNative.macroBankSetLink(it, id, target, up, down) }

    fun removeLink(
        id: Int,
        target: String,
    ) = edit { GeodeNative.macroBankRemoveLink(it, id, target) }

    private fun added(change: (String) -> String): Int? {
        val before = _state.value.knobs.mapTo(mutableSetOf()) { it.id }
        edit(change = change)
        return _state.value.knobs.firstOrNull { it.id !in before }?.id
    }

    /** A drag moves the knob live; [commit] once the gesture ends, so a drag is one write. */
    fun setValue(
        id: Int,
        value: Float,
        persist: Boolean = true,
    ) {
        edit(persist) { GeodeNative.macroBankSetValue(it, id, value) }
    }

    /** Every knob back to centre; the knobs and what they move stay. */
    fun centreAll() {
        val ids = _state.value.knobs.map { it.id }
        edit(persist = true) { text -> ids.fold(text) { bank, id -> GeodeNative.macroBankSetValue(bank, id, 0f) } }
    }

    /** Saves what a drag left behind (see [setValue]). */
    fun commit() = write()

    internal fun edit(
        persist: Boolean = true,
        change: (String) -> String,
    ) {
        val text =
            runCatching { change(_state.value.text) }
                .onFailure { RingLog.note(TAG, "macro bank edit failed", it) }
                .getOrNull() ?: return
        _state.value = stateOf(text)
        push(text)
        if (persist) write()
    }

    private fun load(): MacroBankState {
        if (RETIRED_KEYS.any(prefs::contains)) {
            prefs.edit().apply { RETIRED_KEYS.forEach(::remove) }.apply()
        }
        val stored = prefs.getString(KEY_BANK, null).orEmpty()
        return stateOf(runCatching { normalize(stored) }.getOrDefault(stored))
    }

    private fun write() {
        prefs.edit().putString(KEY_BANK, _state.value.text).apply()
    }

    companion object {
        private const val TAG = "MacroStore"
        private const val KEY_BANK = "macro_bank"

        /** Every key this store writes: a backup files them under Creative, with the presets they shape. */
        internal val KEYS: Set<String> = setOf(KEY_BANK)

        /** The old six knobs' keys: removed on load, and never backed up or restored. */
        internal val RETIRED_KEYS: Set<String> = setOf("macro_knobs", "macro_locks", "macro_presets")

        @Volatile private var instance: MacroStore? = null

        fun get(context: Context): MacroStore =
            instance ?: synchronized(this) {
                instance ?: MacroStore(GeodePrefsFiles(context.applicationContext).general).also { instance = it }
            }

        fun stateOf(text: String): MacroBankState = MacroBankState(text, knobsOf(text))

        /** The `knob|<id>|<value>|<name>|<hint>` rows of a bank text; anything else is skipped. */
        fun knobsOf(text: String): List<MacroKnob> =
            text
                .lineSequence()
                .mapNotNull { line ->
                    val f = line.split('|')
                    if (f.size != KNOB_FIELDS || f[0] != "knob") return@mapNotNull null
                    val id = f[1].toIntOrNull() ?: return@mapNotNull null
                    val value = f[2].toFloatOrNull() ?: return@mapNotNull null
                    MacroKnob(id, f[3], f[4], value)
                }.toList()

        private const val KNOB_FIELDS = 5
    }
}
