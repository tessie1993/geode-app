package dev.geode.render.scene

/**
 * The Customize panel's tabs, in the order the panel shows them.
 *
 * [MACROS] comes first: six knobs laid over every style (core/viz/Macros), kept outside the params and
 * presets, so it owns no parameter either.
 *
 * [EVENTS] comes next: "when this happens in the music, do that" ([dev.geode.render.EventRecipe]),
 * plus whether the engine may pick a new style when the song changes section. It owns no
 * parameter of its own; it is the easy way into the detail tabs after it.
 *
 * [SHAPE], [COLOR], [MOTION], [FX] and [REACTIVITY] are the five the spec asks for and every
 * style has them. [CAMERA] moves the shared camera through the scene and [LAYERS] holds what happens
 * to the picture itself (fall through, fractal growth, melt, spawn, freeze, shatter, particles); every
 * style has those too, and in every tab each parameter is a lane the music plays (ui/MusicLanes). [SCENE] holds the choices about the style itself rather than about a
 * parameter (how it transitions in). The last three are per-family control surfaces the panel
 * only offers when that family is on screen.
 */
enum class CustomizeTab(
    val title: String,
) {
    MACROS("Macros"),
    EVENTS("Events"),

    SHAPE("Shape"),
    COLOR("Colour"),
    MOTION("Motion"),
    CAMERA("Camera"),
    LAYERS("Layers"),
    FX("FX"),
    REACTIVITY("Reactivity"),
    SCENE("Scene"),

    FLUID("Fluid"),

    CYMATICS("Cymatics"),
}
