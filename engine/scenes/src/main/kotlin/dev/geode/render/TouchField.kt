package dev.geode.render

/**
 * The finger slots the native touch field reads (`geode_viz_set_touch`).
 *
 * The field itself (anchor, gesture, axis, spin, decay) is stepped natively on the GL thread
 * (core/viz), next to the scenes that read it; Kotlin only collects up to [MAX_POINTS] pointers
 * and hands them over. ADR 0003 moved the last Kotlin copy of that maths out.
 */
object TouchField {
    /** Slots. Mirrors `uTouchPoints` in lib_scene_uniforms.glsl; changing one alone breaks the upload. */
    const val MAX_POINTS: Int = 5
}
