package dev.geode.render.scene

/**
 * The Customize panel's tabs, in the order the product spec names them.
 *
 * [SHAPE], [COLOR], [MOTION], [FX] and [REACTIVITY] are the five the spec asks for and every
 * style has them. [SCENE] holds the choices about the style itself rather than about a
 * parameter (how it transitions in, whether the engine may pick for you). The last three are
 * per-family control surfaces the panel only offers when that family is on screen.
 */
enum class CustomizeTab(
    val title: String,
) {
    SHAPE("Shape"),
    COLOR("Colour"),
    MOTION("Motion"),
    FX("FX"),
    REACTIVITY("Reactivity"),
    SCENE("Scene"),

    FLUID("Fluid"),

    CYMATICS("Cymatics"),
}
