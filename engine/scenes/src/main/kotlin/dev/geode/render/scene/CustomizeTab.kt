package dev.geode.render.scene

/**
 * The Customize panel's pages, and the engine's roll groups under them.
 *
 * Two kinds of entry share this enum on purpose. The first five are what the panel shows, in
 * order ([PANEL]): simple first, Advanced at the back, so a listener who only wants "make it
 * warmer" never meets an envelope editor (docs/research/REACTIVE_VISUALS.md §7).
 *
 * - [MACROS]: six knobs laid over every style (core/viz/Macros), kept outside the params and
 *   presets, so it owns no parameter.
 * - [LOOK]: what every style shares: palette, hue, grading, speed and motion, trails, glow and
 *   screen effects.
 * - [STYLE]: the style's own controls (shape, symmetry, particles, march detail, endless zoom,
 *   MilkDrop, Fluid, Cymatics). Its title on screen is the style's name.
 * - [EVENTS]: "when this happens in the music, do that" ([dev.geode.render.EventRecipe]). It owns
 *   no parameter; its Randomize is "Shuffle reactions".
 * - [ADVANCED]: the deep editors (sound response, reaction routes, layers, camera, transitions,
 *   gradient maker, flow field, shader code), each a card opening its own page.
 *
 * The rest ([SHAPE] .. [CYMATICS]) are the engine's roll groups: core/viz/ParamRandomizer files
 * every control under one of those names (`TAB|key` lines), and the saved locks and tests name
 * them, so they keep their names even though no panel page shows them as a tab any more. A panel
 * page rolls and resets the groups in [groups], minus the controls it hands to another page
 * ([gives]) plus the ones it takes from one ([takes]); [ParamRandomizer] does the arithmetic.
 */
enum class CustomizeTab(
    val title: String,
) {
    MACROS("Macros"),
    LOOK("Look"),
    STYLE("Style"),
    EVENTS("Events"),
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
                LOOK -> listOf(COLOR, MOTION, FX)
                STYLE -> listOf(SHAPE, SCENE, FLUID, CYMATICS)
                ADVANCED -> listOf(REACTIVITY)
                MACROS, EVENTS -> emptyList()
                else -> listOf(this)
            }

    /** Controls whose engine group is in [groups] but which this page leaves to another page. */
    val gives: Set<String>
        get() =
            when (this) {
                LOOK -> CAMERA_KEYS + STYLE_MOTION_KEYS
                STYLE -> ALL_STYLES_FLOW_KEYS
                else -> emptySet()
            }

    /** Controls this page owns although the engine files them under a group outside [groups]. */
    val takes: Set<String>
        get() =
            when (this) {
                STYLE -> STYLE_MOTION_KEYS
                ADVANCED -> CAMERA_KEYS + ALL_STYLES_FLOW_KEYS
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
        val PANEL: List<CustomizeTab> = listOf(MACROS, LOOK, STYLE, EVENTS, ADVANCED)

        /** The camera rig: Motion in the engine, Advanced › Camera on screen. */
        val CAMERA_KEYS: Set<String> =
            setOf(ParamKeys.CAM_PATH, ParamKeys.CAM_FLY, ParamKeys.CAM_ORBIT, ParamKeys.CAM_FOV, ParamKeys.CAM_SHAKE)

        /** Motion controls only some styles have, so they sit with the style's own controls. */
        val STYLE_MOTION_KEYS: Set<String> = setOf(ParamKeys.ENDLESS_ZOOM, ParamKeys.DIVE_SPEED, ParamKeys.TURBULENCE)

        /** The flow field and ripple overlay ride on every style: Advanced, not the style's page. */
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
