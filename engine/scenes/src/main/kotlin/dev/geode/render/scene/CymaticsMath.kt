package dev.geode.render.scene

/**
 * The cymatics ranges the Customize sliders and the randomizer offer.
 *
 * The plate maths (band-to-mode map, Bessel dish, ring times) runs natively in
 * core/viz/scenes/CymaticsScene and `cymatics_field_frag.glsl`; ADR 0003 removed the unused
 * Kotlin copy. These bounds move native with the catalogue metadata.
 */
object CymaticsMath {
    const val MAX_RENDERED_MODES: Int = 8

    const val MIN_FUNDAMENTAL_HZ: Float = 40f
    const val MAX_FUNDAMENTAL_HZ: Float = 440f
}
