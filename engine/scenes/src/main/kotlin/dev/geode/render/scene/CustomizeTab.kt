package dev.geode.render.scene

/**
 * The Customize panel's tabs, in the order the panel shows them.
 *
 * [MACROS] comes first: six knobs that each move several parameters ([MacroSet]). It owns no
 * parameter of its own; it is the easy way into the detail tabs after it.
 *
 * [SHAPE], [COLOR], [MOTION], [FX] and [REACTIVITY] are the five the spec asks for and every
 * style has them. [SCENE] holds the choices about the style itself rather than about a
 * parameter (how it transitions in, whether the engine may pick for you). The last three are
 * per-family control surfaces the panel only offers when that family is on screen.
 */
enum class CustomizeTab(
    val title: String,
) {
    MACROS("Macros"),

    SHAPE("Shape"),
    COLOR("Colour"),
    MOTION("Motion"),
    FX("FX"),
    REACTIVITY("Reactivity"),
    SCENE("Scene"),

    FLUID("Fluid"),

    CYMATICS("Cymatics"),
}
