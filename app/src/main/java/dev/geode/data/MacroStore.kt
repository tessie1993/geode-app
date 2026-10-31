package dev.geode.data

import android.content.Context
import android.content.SharedPreferences
import dev.geode.RingLog
import dev.geode.engine.bridge.GeodeNative
import dev.geode.util.bestEffort
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.update

/** One saved set of the six Customize › Macros knobs. */
data class MacroPreset(
    val name: String,
    val knobs: List<Float>,
)

/** The knobs (-1..1, 0 = as styled), which ones Surprise leaves alone, and the user's own presets. */
data class MacroState(
    val knobs: List<Float> = List(MacroStore.COUNT) { 0f },
    val lockMask: Int = 0,
    val presets: List<MacroPreset> = emptyList(),
) {
    fun isLocked(i: Int): Boolean = lockMask and (1 shl i) != 0
}

/**
 * Customize › Macros' storage, process-wide.
 *
 * The knobs are a layer over every style (core/viz/Macros does all the work), so they live outside
 * look presets: they survive a style change, and the screen, exports, the wallpaper and the dream all
 * read the one copy the engine holds. Every change is handed to native at once; [GeodeApp] pushes the
 * saved knobs at start, so a wallpaper that starts before the app does still sees them.
 */
class MacroStore private constructor(
    private val prefs: SharedPreferences,
) {
    private val _state = MutableStateFlow(read())
    val state: StateFlow<MacroState> = _state

    init {
        push(_state.value.knobs)
    }

    /** A drag moves the knob live; [persist] once the gesture ends, so a drag is one write. */
    fun setKnob(
        i: Int,
        value: Float,
        persist: Boolean = true,
    ) {
        if (i !in 0 until COUNT) return
        val v = value.coerceIn(-1f, 1f)
        _state.update { it.copy(knobs = it.knobs.toMutableList().also { k -> k[i] = v }) }
        push(_state.value.knobs)
        if (persist) write()
    }

    fun setKnobs(knobs: List<Float>) {
        _state.update { it.copy(knobs = List(COUNT) { i -> knobs.getOrElse(i) { 0f }.coerceIn(-1f, 1f) }) }
        push(_state.value.knobs)
        write()
    }

    fun resetKnobs() = setKnobs(emptyList())

    /** Saves what a drag left behind (see [setKnob]). */
    fun commit() = write()

    fun toggleLock(i: Int) {
        _state.update { it.copy(lockMask = it.lockMask xor (1 shl i)) }
        write()
    }

    /** Every unlocked knob to a new spot (core/viz/Macros, seeded from [seed]). */
    fun surprise(seed: Long = System.nanoTime()) {
        val s = _state.value
        runCatching { GeodeNative.macrosSurprise(s.knobs.toFloatArray(), s.lockMask, seed) }
            .onSuccess { setKnobs(it.toList()) }
            .onFailure { RingLog.note(TAG, "macro surprise failed", it) }
    }

    /** Saves the knobs as [name], replacing a preset of the same name. */
    fun savePreset(name: String) {
        val clean = name.replace('|', ' ').replace('\n', ' ').trim()
        if (clean.isEmpty()) return
        _state.update { s -> s.copy(presets = s.presets.filterNot { it.name == clean } + MacroPreset(clean, s.knobs)) }
        write()
    }

    fun deletePreset(name: String) {
        _state.update { s -> s.copy(presets = s.presets.filterNot { it.name == name }) }
        write()
    }

    private fun read(): MacroState =
        MacroState(
            knobs = parseKnobs(prefs.getString(KEY_KNOBS, null)) ?: List(COUNT) { 0f },
            lockMask = prefs.getInt(KEY_LOCKS, 0),
            presets = parsePresets(prefs.getString(KEY_PRESETS, null).orEmpty()),
        )

    private fun write() {
        val s = _state.value
        prefs
            .edit()
            .putString(KEY_KNOBS, s.knobs.joinToString(","))
            .putInt(KEY_LOCKS, s.lockMask)
            .putString(KEY_PRESETS, formatPresets(s.presets))
            .apply()
    }

    private fun push(knobs: List<Float>) {
        bestEffort(TAG, "push macros") { GeodeNative.macrosSet(knobs.toFloatArray()) }
    }

    companion object {
        const val COUNT = 6

        private const val TAG = "MacroStore"
        private const val KEY_KNOBS = "macro_knobs"
        private const val KEY_LOCKS = "macro_locks"
        private const val KEY_PRESETS = "macro_presets"

        @Volatile private var instance: MacroStore? = null

        fun get(context: Context): MacroStore =
            instance ?: synchronized(this) {
                instance ?: MacroStore(GeodePrefsFiles(context.applicationContext).general).also { instance = it }
            }

        /** The built-in presets from core/viz/Macros, "As styled" first. */
        fun starters(): List<MacroPreset> =
            runCatching { parsePresets(GeodeNative.macroStarters()) }.getOrDefault(emptyList())

        /** "Name|v,v,v,v,v,v" lines; a malformed line is skipped. */
        fun parsePresets(text: String): List<MacroPreset> =
            text.lineSequence().mapNotNull { line ->
                val bar = line.lastIndexOf('|')
                if (bar <= 0) return@mapNotNull null
                parseKnobs(line.substring(bar + 1))?.let { MacroPreset(line.substring(0, bar), it) }
            }.toList()

        fun formatPresets(presets: List<MacroPreset>): String =
            presets.joinToString("\n") { p -> p.name + "|" + p.knobs.joinToString(",") }

        private fun parseKnobs(text: String?): List<Float>? {
            val v = text?.split(',')?.map { it.trim().toFloatOrNull() ?: return null } ?: return null
            return if (v.size == COUNT) v.map { it.coerceIn(-1f, 1f) } else null
        }
    }
}
