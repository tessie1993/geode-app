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

/** One choice Customize › Layers offers: its [name] as native takes it back, and its [label]. */
data class LayerChoice(
    val name: String,
    val label: String,
)

/** A Styles page family the layer's style is picked from: its catalogue [name] and its styles' ids. */
data class LayerFamily(
    val name: String,
    val ids: List<String>,
)

/** Customize › Layers as the page draws it; every field is read from native's editor text (core/viz/LayerPick). */
data class LayersEditor(
    val on: Boolean = false,
    val source: String = "",
    val styleId: String = "",
    val knob: Int = 0,
    val blend: Int = GeodeNative.LAYER_BLEND_DEFAULT,
    val mix: Float = DEFAULT_MIX,
    /** The style the layer draws now, "" for none. */
    val shown: String = "",
    val sources: List<LayerChoice> = emptyList(),
    val blends: List<String> = emptyList(),
    val families: List<LayerFamily> = emptyList(),
    val knobs: List<LayerChoice> = emptyList(),
) {
    companion object {
        private const val DEFAULT_MIX = 0.5f
        private const val ON = 1
        private const val SOURCE = 2
        private const val STYLE = 3
        private const val KNOB = 4
        private const val BLEND = 5
        private const val MIX = 6

        /** Splits native's rows: `state`, `shown`, `source`, `blend`, `family` and `knob`; anything else is skipped. */
        fun parse(text: String): LayersEditor {
            var editor = LayersEditor()
            val sources = mutableListOf<LayerChoice>()
            val blends = mutableListOf<String>()
            val families = mutableListOf<LayerFamily>()
            val knobs = mutableListOf<LayerChoice>()
            text.lineSequence().forEach { line ->
                val f = line.split('|')
                when (f[0]) {
                    "state" ->
                        editor =
                            editor.copy(
                                on = f.getOrNull(ON) == "1",
                                source = f.getOrNull(SOURCE).orEmpty(),
                                styleId = f.getOrNull(STYLE).orEmpty(),
                                knob = f.getOrNull(KNOB)?.toIntOrNull() ?: 0,
                                blend = f.getOrNull(BLEND)?.toIntOrNull() ?: GeodeNative.LAYER_BLEND_DEFAULT,
                                mix = f.getOrNull(MIX)?.toFloatOrNull() ?: DEFAULT_MIX,
                            )
                    "shown" -> editor = editor.copy(shown = f.getOrNull(1).orEmpty())
                    "source" -> sources += LayerChoice(f.getOrNull(1).orEmpty(), f.getOrNull(2).orEmpty())
                    "blend" -> blends += f.getOrNull(2).orEmpty()
                    "family" -> {
                        val ids = f.getOrNull(2).orEmpty().split(',').filter { it.isNotEmpty() }
                        families += LayerFamily(f.getOrNull(1).orEmpty(), ids)
                    }
                    "knob" -> knobs += LayerChoice(f.getOrNull(1).orEmpty(), f.getOrNull(2).orEmpty())
                }
            }
            return editor.copy(sources = sources, blends = blends, families = families, knobs = knobs)
        }
    }
}

/**
 * Customize › Layers' storage, process-wide: the layer (core/viz/LayerState) as the one line native writes.
 * Every edit is a native call that returns the new line; this keeps the line, hands it to every renderer at
 * once and saves it, as [MacroStore] and [PaletteStackStore] keep theirs. Which style the layer draws is
 * native's (core/viz/LayerPick), and so is everything the page offers ([editor]).
 *
 * One instance per process, built by `GeodeContainer`: the saved layer is handed to the engine as soon as
 * it is read, so the app, the wallpaper and the dream draw it after a restart ([LookStore.pushProcessWide]
 * also pushes it for a process the app's session never started).
 */
class LayerStore internal constructor(
    private val repository: SettingsRepository<String>,
    private val scope: CoroutineScope,
    private val push: (String) -> Unit = { text -> bestEffort(TAG, "push layer") { GeodeNative.layerSet(text) } },
) {
    private val _text = MutableStateFlow("")
    val text: StateFlow<String> = _text

    init {
        scope.launch {
            val saved = repository.loaded()
            _text.value = saved
            push(saved)
        }
    }

    /** What the page draws and offers over [activeStyle], for the layer as it is now. */
    fun editor(activeStyle: String): LayersEditor =
        runCatching { LayersEditor.parse(GeodeNative.layerEditor(_text.value, activeStyle)) }.getOrDefault(LayersEditor())

    /** One field changed by native (`geode_layer_edit`: on, source, style, knob, blend, mix), live and saved. */
    fun edit(
        field: String,
        value: String,
    ) {
        val text = runCatching { GeodeNative.layerEdit(_text.value, field, value) }.getOrNull() ?: return
        if (text.isEmpty() || text == _text.value) return
        _text.value = text
        push(text)
        scope.launch { repository.update { text } }
    }

    companion object {
        private const val TAG = "LayerStore"
        internal const val KEY_LAYER = "customize_layer"

        /** A backup files the layer under Creative, with the looks it is drawn over. */
        internal val KEYS: Set<String> = setOf(KEY_LAYER)

        internal val PREF_KEYS = prefKeys(GeodePrefsFiles.GENERAL) { portable(PrefType.STRING, KEY_LAYER) }
    }
}

/** The saved layer in `geode-prefs`: one line, normalized by native when it is read (no line is the layer off). */
class LayerCodec(
    private val normalize: (String) -> String = { GeodeNative.layerNormalize(it) },
) : PrefsCodec<String> {
    override val defaults = ""

    override val keys get() = LayerStore.PREF_KEYS

    override fun read(prefs: SharedPreferences): String {
        val stored = prefs.getString(LayerStore.KEY_LAYER, null).orEmpty()
        return runCatching { normalize(stored) }.getOrDefault(stored)
    }

    override fun write(
        editor: SharedPreferences.Editor,
        value: String,
    ) {
        editor.putString(LayerStore.KEY_LAYER, value)
    }
}
