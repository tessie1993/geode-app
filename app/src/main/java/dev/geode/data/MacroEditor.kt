package dev.geode.data

/**
 * One param a knob moves: [up] and [down] are -1..1 of the target's range at a full turn each way, the
 * link's own range; [upBy] and [downBy] are the same offsets in the param's units, as native works them out.
 */
data class MacroLink(
    val target: String,
    val up: Float,
    val down: Float,
    /** False when the style on screen has no such param: the link stays, and shows dimmed. */
    val reaches: Boolean,
    /** How the link follows the knob: one of [MacroEditorState.curves]. */
    val curve: String = LINEAR,
    val upBy: Float = 0f,
    val downBy: Float = 0f,
) {
    companion object {
        const val LINEAR: String = "LINEAR"
    }
}

/** A knob as the editor draws it; what it offers ([builtIn], [asBuilt], [canLink]) is native's answer. */
data class MacroEditorKnob(
    val id: Int,
    /** A built-in knob can be reset (unless [asBuilt]) and never removed; any other can be removed. */
    val builtIn: Boolean,
    val asBuilt: Boolean,
    val canLink: Boolean,
    val value: Float,
    val name: String,
    val hint: String,
    val links: List<MacroLink>,
)

/** A knob in the bank's library, off screen: what Add macro offers to put back. */
data class MacroLibraryKnob(
    val id: Int,
    val builtIn: Boolean,
    val name: String,
    val hint: String,
)

/** A target a link may name, by its modulation wire name, and whether the style on screen shows it. */
data class MacroTarget(
    val name: String,
    val reaches: Boolean,
)

data class MacroEditorState(
    val maxKnobs: Int = 0,
    val maxLinks: Int = 0,
    val maxNameBytes: Int = 0,
    /** A new knob fits on screen and in the bank. */
    val canAdd: Boolean = false,
    /** A knob from [library] fits on screen. */
    val canShow: Boolean = false,
    /** The four built-in knobs as built (their turn aside), so Reset all has nothing to do. */
    val isDefault: Boolean = true,
    /** The knobs on screen, in bank order. */
    val knobs: List<MacroEditorKnob> = emptyList(),
    val library: List<MacroLibraryKnob> = emptyList(),
    val targets: List<MacroTarget> = emptyList(),
    /** Every curve a link can take, in the order to offer them. */
    val curves: List<String> = emptyList(),
)

/** A Customize slider a knob can move: its modulation [target], its Customize [category] and the knobs that move it now. */
data class MacroControl(
    val target: String,
    val category: String,
    val knobIds: List<Int>,
)

/**
 * Which Customize sliders a knob can move (core/viz/MacroBank's controlsText, `geode_macro_bank_controls`),
 * by ParamKeys name. A slider missing here cannot be put on a knob.
 */
object MacroControlsTable {
    private const val VERSION = "1"
    private const val CONTROL_FIELDS = 5

    fun parse(text: String): Map<String, MacroControl> {
        val rows =
            text
                .lineSequence()
                .filter { it.isNotEmpty() }
                .map { it.split('|') }
                .toList()
        if (rows.firstOrNull() != listOf("macrocontrols", VERSION)) return emptyMap()
        return rows
            .drop(1)
            .filter { it.size == CONTROL_FIELDS && it[0] == "control" }
            .associate { f -> f[1] to MacroControl(f[2], f[3], f[4].split(',').mapNotNull(String::toIntOrNull)) }
    }
}

/**
 * The knob editor's read of the macro bank (core/viz/MacroBank's editorText, `geode_macro_bank_editor`):
 * rows split on '|', link rows belonging to the knob row above them. Kotlin decides nothing here; a
 * text of another version, or a row it cannot read, is left out, so an old shell draws less, not wrong.
 */
object MacroEditorTable {
    /** JVM tests read core/tests/macro_editor.txt, the copy core/tests/macro_bank_test keeps current. */
    const val TEXT_FILE_PROPERTY: String = "geode.macroEditor"
    private const val VERSION = "1"

    fun parse(text: String): MacroEditorState {
        val rows =
            text
                .lineSequence()
                .filter { it.isNotEmpty() }
                .map { it.split('|') }
                .toList()
        if (rows.firstOrNull() != listOf("macroeditor", VERSION)) return MacroEditorState()
        var state = MacroEditorState()
        val knobs = mutableListOf<MacroEditorKnob>()
        val targets = mutableListOf<MacroTarget>()
        val curves = mutableListOf<String>()
        val library = mutableListOf<MacroLibraryKnob>()
        for (f in rows.drop(1)) {
            when (f[0]) {
                "caps" ->
                    if (f.size == CAPS_FIELDS) {
                        state = state.copy(maxKnobs = int(f[1]), maxLinks = int(f[2]), maxNameBytes = int(f[3]))
                    }
                "bank" -> if (f.size == BANK_FIELDS) state = state.copy(canAdd = f[1] == ON, isDefault = f[2] == ON)
                "knob" -> knobOf(f)?.let(knobs::add)
                "link" -> attach(knobs, linkOf(f))
                "target" -> if (f.size == TARGET_FIELDS) targets += MacroTarget(f[1], f[2] == ON)
                "curve" -> if (f.size == CURVE_FIELDS) curves += f[1]
                "screen" -> if (f.size == SCREEN_FIELDS) state = state.copy(canShow = f[1] == ON)
                "library" -> libraryOf(f)?.let(library::add)
            }
        }
        return state.copy(knobs = knobs, targets = targets, curves = curves, library = library)
    }

    private fun attach(
        knobs: MutableList<MacroEditorKnob>,
        link: MacroLink?,
    ) {
        val knob = knobs.lastOrNull() ?: return
        if (link != null) knobs[knobs.lastIndex] = knob.copy(links = knob.links + link)
    }

    private fun knobOf(f: List<String>): MacroEditorKnob? {
        if (f.size != KNOB_FIELDS) return null
        val id = f[1].toIntOrNull() ?: return null
        val value = f[5].toFloatOrNull() ?: return null
        return MacroEditorKnob(id, f[2] == ON, f[3] == ON, f[4] == ON, value, f[6], f[7], emptyList())
    }

    private fun libraryOf(f: List<String>): MacroLibraryKnob? {
        if (f.size != LIBRARY_FIELDS) return null
        val id = f[1].toIntOrNull() ?: return null
        return MacroLibraryKnob(id, f[2] == ON, f[3], f[4])
    }

    private fun linkOf(f: List<String>): MacroLink? {
        if (f.size != LINK_FIELDS) return null
        val up = f[2].toFloatOrNull() ?: return null
        val down = f[3].toFloatOrNull() ?: return null
        val upBy = f[6].toFloatOrNull() ?: return null
        val downBy = f[7].toFloatOrNull() ?: return null
        return MacroLink(f[1], up, down, f[4] == ON, f[5], upBy, downBy)
    }

    private fun int(s: String): Int = s.toIntOrNull() ?: 0

    private const val ON = "1"
    private const val CAPS_FIELDS = 4
    private const val BANK_FIELDS = 3
    private const val KNOB_FIELDS = 8
    private const val LINK_FIELDS = 8
    private const val TARGET_FIELDS = 3
    private const val CURVE_FIELDS = 2
    private const val SCREEN_FIELDS = 2
    private const val LIBRARY_FIELDS = 5
}
