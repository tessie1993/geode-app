package dev.geode.data

import android.content.SharedPreferences
import dev.geode.RingLog
import dev.geode.data.prefs.BackupClass
import dev.geode.data.prefs.PrefType
import dev.geode.data.prefs.PrefsCodec
import dev.geode.data.prefs.SettingsRepository
import dev.geode.data.prefs.prefKeys
import dev.geode.engine.bridge.GeodeNative
import dev.geode.util.bestEffort
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlin.random.Random

/** One knob of the macro bank as the shell draws it, read off the bank text; [value] runs -1..1. */
data class MacroKnob(
    val id: Int,
    val name: String,
    val hint: String,
    val value: Float,
)

/**
 * A new knob not saved yet: [text] is the saved bank with it added (what the renderer shows meanwhile) and
 * [id] the new knob's. Saving makes [text] the bank; discarding drops it.
 */
data class MacroDraft(
    val text: String,
    val id: Int,
)

/** The bank as stored ([text], opaque to Kotlin), the knobs its `knob|` rows list in bank order, and a draft. */
data class MacroBankState(
    val text: String = "",
    val knobs: List<MacroKnob> = emptyList(),
    val draft: MacroDraft? = null,
) {
    /** What edits apply to and the renderer shows: the draft while there is one, else the saved bank. */
    val working: String get() = draft?.text ?: text
}

/**
 * Quick knobs' storage, process-wide: the macro bank (core/viz/MacroBank) as the one string native
 * writes. Every edit (a turn, a name, a link, a new or removed knob) is a native call that returns the
 * new bank; this keeps the text, reads the knobs off it for drawing, hands it to the engine at once and
 * saves it. A new knob is a draft ([newDraft]): it is live on the visual while it is edited, but saved only
 * by [saveDraft] under the name the user gives it, and [discardDraft] drops it.
 *
 * One instance per process, built by `GeodeContainer`: the saved bank is a [SettingsRepository] over
 * [MacroBankCodec] (#1009, PR 6), read and normalized on IO at process start and handed to the engine
 * as soon as it is back; the bank is empty until then. Saves go through the repository in the order
 * they were made.
 *
 * The six old knobs, their locks and saved sets are gone with no migration: their keys are dropped
 * the first time the bank is read ([RETIRED_KEYS]).
 */
class MacroStore internal constructor(
    private val repository: SettingsRepository<String>,
    private val scope: CoroutineScope,
    private val push: (String) -> Unit = { text -> bestEffort(TAG, "push macro bank") { GeodeNative.macroBankSet(text) } },
) {
    private val _state = MutableStateFlow(MacroBankState())
    val state: StateFlow<MacroBankState> = _state

    init {
        scope.launch {
            val saved = repository.loaded()
            // Nothing can have edited the bank before its first read: every edit starts from it.
            _state.value = stateOf(saved)
            push(saved)
        }
    }

    /** What the knob editor draws and offers on [styleId], as native lists it for the bank as it is now. */
    fun editor(styleId: String): MacroEditorState =
        runCatching { MacroEditorTable.parse(GeodeNative.macroBankEditor(_state.value.working, styleId)) }
            .onFailure { RingLog.note(TAG, "macro editor read failed", it) }
            .getOrDefault(MacroEditorState())

    /** Which Customize sliders a knob can move, and which knobs move each, for the bank as it is now. */
    fun controls(): Map<String, MacroControl> =
        runCatching { MacroControlsTable.parse(GeodeNative.macroBankControls(_state.value.working)) }
            .onFailure { RingLog.note(TAG, "macro controls read failed", it) }
            .getOrDefault(emptyMap())

    fun rename(
        id: Int,
        name: String,
    ) = edit { GeodeNative.macroBankRename(it, id, name) }

    /** A new empty knob as a draft (any earlier draft dropped); its id, or null when the bank is full. */
    fun newDraft(): Int? = drafted { GeodeNative.macroBankAddKnob(it, "") }

    /** A draft knob that already moves the Customize slider [control]; its id, or null when the bank is full. */
    fun newDraftFor(control: String): Int? = drafted { GeodeNative.macroBankAddKnobFor(it, control) }

    /** The draft saved as a knob named [name] (native cleans it, and keeps its own name for an empty one). */
    fun saveDraft(name: String) {
        val draft = _state.value.draft ?: return
        val text = runCatching { GeodeNative.macroBankRename(draft.text, draft.id, name) }.getOrDefault(draft.text)
        _state.value = stateOf(text)
        push(text)
        write()
    }

    fun discardDraft() {
        if (_state.value.draft == null) return
        _state.value = _state.value.copy(draft = null)
        push(_state.value.text)
    }

    /** The name native offers when the knob is saved. */
    fun autoName(id: Int): String = runCatching { GeodeNative.macroBankAutoName(_state.value.working, id) }.getOrDefault("")

    /** A new random set of links for the knob, a new roll each call. */
    fun roll(id: Int) = edit { GeodeNative.macroBankRoll(it, id, Random.nextLong()) }

    /** A new turn for every knob on screen that moves something on [styleId], a new roll each call. */
    fun rollValues(styleId: String) = edit { GeodeNative.macroBankRollValues(it, styleId, Random.nextLong()) }

    /** Off screen into the bank's library; Add macro offers it back. */
    fun hide(id: Int) = edit { GeodeNative.macroBankHideKnob(it, id) }

    /** From the library back on screen, last. */
    fun show(id: Int) = edit { GeodeNative.macroBankShowKnob(it, id) }

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

    fun setCurve(
        id: Int,
        target: String,
        curve: String,
    ) = edit { GeodeNative.macroBankSetCurve(it, id, target, curve) }

    private fun added(change: (String) -> String): Int? {
        val before = knobsOf(_state.value.working).mapTo(mutableSetOf()) { it.id }
        edit(change = change)
        return knobsOf(_state.value.working).firstOrNull { it.id !in before }?.id
    }

    internal fun drafted(change: (String) -> String): Int? {
        val saved = _state.value.text
        val text = runCatching { change(saved) }.onFailure { RingLog.note(TAG, "macro draft failed", it) }.getOrNull() ?: return null
        val before = _state.value.knobs.mapTo(mutableSetOf()) { it.id }
        val id = knobsOf(text).firstOrNull { it.id !in before }?.id ?: return null
        _state.value = _state.value.copy(draft = MacroDraft(text, id))
        push(text)
        return id
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
        val ids = knobsOf(_state.value.working).map { it.id }
        edit(persist = true) { text -> ids.fold(text) { bank, id -> GeodeNative.macroBankSetValue(bank, id, 0f) } }
    }

    /** Saves what a drag left behind (see [setValue]). */
    fun commit() = write()

    internal fun edit(
        persist: Boolean = true,
        change: (String) -> String,
    ) {
        val current = _state.value
        val text =
            runCatching { change(current.working) }
                .onFailure { RingLog.note(TAG, "macro bank edit failed", it) }
                .getOrNull() ?: return
        val draft = current.draft
        if (draft != null) {
            // A draft is live but not saved; a draft that lost its knob (deleted) is gone.
            val kept = knobsOf(text).any { it.id == draft.id }
            _state.value = if (kept) current.copy(draft = draft.copy(text = text)) else current.copy(draft = null)
            push(if (kept) text else current.text)
            return
        }
        _state.value = stateOf(text)
        push(text)
        if (persist) write()
    }

    private fun write() {
        if (_state.value.draft != null) return
        val text = _state.value.text
        scope.launch { repository.update { text } }
    }

    companion object {
        private const val TAG = "MacroStore"
        internal const val KEY_BANK = "macro_bank"

        /** Every key this store writes: a backup files them under Creative, with the presets they shape. */
        internal val KEYS: Set<String> = setOf(KEY_BANK)

        /** The old six knobs' keys: removed on load, and never backed up or restored. */
        internal val RETIRED_KEYS: Set<String> = setOf("macro_knobs", "macro_locks", "macro_presets")

        internal val PREF_KEYS =
            prefKeys(GeodePrefsFiles.GENERAL) {
                portable(PrefType.STRING, KEY_BANK)
                // Read once to drop them, never restored: BackupFormat files them device-local.
                retired(PrefType.STRING, "macro_knobs", "macro_presets", backup = BackupClass.DEVICE)
                retired(PrefType.INT, "macro_locks", backup = BackupClass.DEVICE)
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

/**
 * The saved bank in `geode-prefs`: one string, normalized by native when it is read (an empty or old
 * bank becomes the built-in one). Reading also drops the six old knobs' keys, as the store's load did.
 */
class MacroBankCodec(
    private val normalizeBank: (String) -> String = { GeodeNative.macroBankNormalize(it) },
) : PrefsCodec<String> {
    override val defaults = ""

    override val keys get() = MacroStore.PREF_KEYS

    override fun read(prefs: SharedPreferences): String {
        if (MacroStore.RETIRED_KEYS.any(prefs::contains)) {
            prefs.edit().apply { MacroStore.RETIRED_KEYS.forEach { remove(it) } }.apply()
        }
        val stored = prefs.getString(MacroStore.KEY_BANK, null).orEmpty()
        return runCatching { normalizeBank(stored) }.getOrDefault(stored)
    }

    override fun write(
        editor: SharedPreferences.Editor,
        value: String,
    ) {
        editor.putString(MacroStore.KEY_BANK, value)
    }
}
