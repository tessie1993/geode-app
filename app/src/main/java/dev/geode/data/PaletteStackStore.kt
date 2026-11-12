package dev.geode.data

import android.content.SharedPreferences
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

/** A palette of the native library, by its place in it. */
data class PalettePick(
    val index: Int,
    val name: String,
)

/** The stack as Customize › Look draws it; every field is read from native's editor text. */
data class PaletteStackState(
    val text: String = "",
    val layers: List<String> = emptyList(),
    val room: Int = 0,
    val blend: String = "",
    val picks: List<PalettePick> = emptyList(),
)

/**
 * The palette stack (Customize › Look › Palettes): the palettes every style takes its colours from, in
 * order, process-wide like the macro bank. The stack is native's text and every edit is a native call;
 * this store keeps the text and hands it to native on load and after each edit. An unset stack draws
 * the look's own palette. Each layer's amount is the look's (`SceneParams.paletteAmount0..7`).
 */
class PaletteStackStore internal constructor(
    private val repository: SettingsRepository<String>,
    private val scope: CoroutineScope,
    private val push: (String) -> Unit = { text -> bestEffort(TAG, "push palette stack") { GeodeNative.paletteStackSet(text) } },
    private val editorOf: (String) -> String = { GeodeNative.paletteStackEditor(it) },
) {
    private val _state = MutableStateFlow(PaletteStackState())
    val state: StateFlow<PaletteStackState> = _state

    init {
        scope.launch {
            val saved = repository.loaded()
            _state.value = stateOf(saved)
            push(saved)
        }
    }

    fun add(libraryIndex: Int) = edit { GeodeNative.paletteStackAddLibrary(it, libraryIndex) }

    fun addColours(
        name: String,
        rgb: List<Int>,
    ) = edit { GeodeNative.paletteStackAddColours(it, name, rgb.toIntArray()) }

    fun remove(layer: Int) = edit { GeodeNative.paletteStackRemove(it, layer) }

    fun move(
        layer: Int,
        delta: Int,
    ) = edit { GeodeNative.paletteStackMove(it, layer, delta) }

    fun setBlend(blend: String) = edit { GeodeNative.paletteStackSetBlend(it, blend) }

    private fun edit(change: (String) -> String) {
        val text = runCatching { change(_state.value.text) }.getOrNull() ?: return
        if (text == _state.value.text) return
        _state.value = stateOf(text)
        push(text)
        scope.launch { repository.update { text } }
    }

    private fun stateOf(text: String): PaletteStackState = parse(text, runCatching { editorOf(text) }.getOrDefault(""))

    companion object {
        private const val TAG = "PaletteStackStore"
        internal const val KEY_STACK = "palette_stack"

        /** A backup files the stack under Creative, with the looks it colours. */
        internal val KEYS: Set<String> = setOf(KEY_STACK)

        internal val PREF_KEYS = prefKeys(GeodePrefsFiles.GENERAL) { portable(PrefType.STRING, KEY_STACK) }

        /** Splits native's editor text: the `palettestack` header, then `layer` and `pick` rows. */
        fun parse(
            text: String,
            editor: String,
        ): PaletteStackState {
            var room = 0
            var blend = ""
            val layers = mutableListOf<String>()
            val picks = mutableListOf<PalettePick>()
            editor.lineSequence().forEach { line ->
                val f = line.split('|')
                when (f[0]) {
                    "palettestack" -> {
                        room = f.getOrNull(ROOM_FIELD)?.toIntOrNull() ?: 0
                        blend = f.getOrNull(BLEND_FIELD).orEmpty()
                    }
                    "layer" -> layers += f.getOrNull(NAME_FIELD).orEmpty()
                    "pick" -> f.getOrNull(1)?.toIntOrNull()?.let { picks += PalettePick(it, f.getOrNull(NAME_FIELD).orEmpty()) }
                }
            }
            return PaletteStackState(text, layers, room, blend, picks)
        }

        private const val NAME_FIELD = 2
        private const val ROOM_FIELD = 3
        private const val BLEND_FIELD = 4
    }
}

/** The saved stack in `geode-prefs`: one string, normalized by native when it is read. */
class PaletteStackCodec(
    private val normalize: (String) -> String = { GeodeNative.paletteStackNormalize(it) },
) : PrefsCodec<String> {
    override val defaults = ""

    override val keys get() = PaletteStackStore.PREF_KEYS

    override fun read(prefs: SharedPreferences): String {
        val stored = prefs.getString(PaletteStackStore.KEY_STACK, null).orEmpty()
        return runCatching { normalize(stored) }.getOrDefault(stored)
    }

    override fun write(
        editor: SharedPreferences.Editor,
        value: String,
    ) {
        editor.putString(PaletteStackStore.KEY_STACK, value)
    }
}
