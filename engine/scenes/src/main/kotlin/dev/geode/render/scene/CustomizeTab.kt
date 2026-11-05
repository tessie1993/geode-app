package dev.geode.render.scene

/**
 * The Customize panel's pages, and the engine's roll groups under them.
 *
 * Two kinds of entry share this enum on purpose. The panel shows seven pages, by kind, in one
 * scrolling list ([PANEL]): the simple ones first, the Pro ones at the back, so a listener who only
 * wants "make it warmer" never meets an envelope editor.
 *
 * - [MACROS]: six knobs laid over every style (core/viz/Macros), kept outside the params and
 *   presets, so it owns no parameter. Its macro sets open from it.
 * - [LOOK]: colour, palette, grading, glow, trails and the screen effects.
 * - [STYLE]: the style's own controls (shape, symmetry, particles, march detail, MilkDrop, Fluid,
 *   Cymatics).
 * - [MOTION]: everything that moves: speed and drift, zoom and the endless dive, inner motion, the
 *   camera, Liquid and the water ripples. It is also the engine's Motion group, which already
 *   holds the camera and the dive, so only the all-styles flow controls change hands to reach it.
 * - [MUSIC]: how the music moves the picture: the event recipes, how it listens, notes and key.
 *   Its Randomize is a new set of reactions.
 * - [MODULATE]: the reaction routes, envelopes and waves. Pro.
 * - [ADVANCED]: layers, transitions and shader code. Pro.
 *
 * The rest ([SHAPE] .. [CYMATICS], [MOTION] among them) are the engine's roll groups:
 * core/viz/ParamRandomizer files every control under one of those names (`TAB|key` lines), and
 * the saved locks and tests name them, so they keep their names. A panel page rolls and resets the
 * groups in [groups], minus the controls it hands to another page ([gives]) plus the ones it takes
 * from one ([takes]); [ParamRandomizer] does the arithmetic.
 */
enum class CustomizeTab(
    val title: String,
) {
    MACROS("Macros"),
    LOOK("Look"),
    STYLE("Style"),
    MUSIC("Music"),
    MODULATE("Modulate"),
    ADVANCED("Advanced"),

    SHAPE("Shape"),
    COLOR("Colour"),
    MOTION("Motion"),
    FX("Effects"),
    REACTIVITY("Sound response"),
    SCENE("Scene"),
    FLUID("Fluid"),
    CYMATICS("Cymatics"),
    ;

    /** The engine roll groups this page rolls and resets. Empty for the pages that own no parameter. */
    val groups: List<CustomizeTab>
        get() =
            when (this) {
                LOOK -> listOf(COLOR, FX)
                STYLE -> listOf(SHAPE, SCENE, FLUID, CYMATICS)
                MUSIC -> listOf(REACTIVITY)
                MACROS, MODULATE, ADVANCED -> emptyList()
                else -> listOf(this)
            }

    /** Controls whose engine group is in [groups] but which this page leaves to another page. */
    val gives: Set<String>
        get() =
            when (this) {
                STYLE -> ALL_STYLES_FLOW_KEYS
                else -> emptySet()
            }

    /** Controls this page owns although the engine files them under a group outside [groups]. */
    val takes: Set<String>
        get() =
            when (this) {
                MOTION -> ALL_STYLES_FLOW_KEYS
                else -> emptySet()
            }

    /**
     * The rolled controls this page owns, given the engine's `group -> key` table. Pure, so the
     * hand-off between pages is tested without the native library.
     */
    fun ownedKeys(rollKeys: List<Pair<String, String>>): Set<String> =
        rollKeys
            .filter { (group, key) ->
                (groups.any { it.name == group } && key !in gives) || key in takes
            }.mapTo(mutableSetOf()) { it.second }

    companion object {
        /** The pages the panel shows, in order. */
        val PANEL: List<CustomizeTab> = listOf(MACROS, LOOK, STYLE, MOTION, MUSIC, MODULATE, ADVANCED)

        /** The flow field and ripple overlay ride on every style: Motion, not the style's page. */
        val ALL_STYLES_FLOW_KEYS: Set<String> =
            setOf(
                ParamKeys.FLOW_STRENGTH,
                ParamKeys.FLOW_FORCE,
                ParamKeys.FLOW_CURL,
                ParamKeys.LIQUID_BORN,
                ParamKeys.LIQUID_AGE,
                ParamKeys.LIQUID_SURFACE,
                ParamKeys.RIPPLE_OVERLAY_STRENGTH,
                ParamKeys.RIPPLE_GLINT,
            )
    }
}
