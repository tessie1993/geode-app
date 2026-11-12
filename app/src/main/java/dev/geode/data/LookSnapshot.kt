package dev.geode.data

import dev.geode.engine.bridge.GeodeNative
import dev.geode.render.scene.SceneParams

/**
 * The look snapshot: everything the user set that decides what a renderer draws, as one immutable value
 * (#345). Not the auto-director's Look (GLOSSARY), which is one decision made inside the engine.
 *
 * Every renderer host (live Visuals, the wallpaper, the dream, Cast, an export) only says where its look
 * comes from ([LookSource]) and what it overrides ([gentleFlight]); the host hands it whole to native
 * (`geode_viz_set_look`, `geode_stage_set_look`), which passes on only the parts that changed. Before this,
 * each host rebuilt it field by field and they drifted: the wallpaper and dream once drew with every LFO,
 * envelope and custom reaction off.
 *
 * Being a value is what pins an export: a render takes one look when it starts, and later edits never reach
 * it. The waves, the envelopes and the routes are their saved texts, read by native alone.
 *
 * Not here, being process-wide native state: the macro bank, Note & key, the director and the hand mask
 * (`EngineSettings`, pinned natively by an export), the layer (#602) and custom shader code (#498).
 */
data class LookSnapshot(
    /** Until a look is pushed, native's first one (`core/viz/LookSnapshot.hpp`): the app's first style, not a Pro one. */
    val sceneId: String = NativeLookDefaults.sceneId,
    val params: SceneParams = SceneParams.DEFAULT,
    /** The waves and the envelopes as saved; empty is nothing saved, which native reads as every slot off. */
    val lfos: String = "",
    val adsrs: String = "",
    /** The user's own reaction routes, or null to play the style's DNA (native merges under the hand mask). */
    val reactions: String? = null,
    /** Settings › Audio's saved tuning, null for the engine's defaults. */
    val soundTuning: List<Float>? = null,
    /** Vestibular accessibility, and the dream's gentle flight; the flash clamp is native and unconditional. */
    val reducedMotion: Boolean = false,
    /** Native's id (core/viz/Transition); a requested cut becomes a fade natively (core/viz/VisualSafety). */
    val transitionId: String = NativeLookDefaults.transitionId,
    val transitionMs: Long = NativeLookDefaults.transitionMs,
    val underlay: LookUnderlay? = null,
) {
    /** The dream's override: the camera keeps flying without big moves, because it is watched from across a room. */
    fun gentleFlight(): LookSnapshot = copy(reducedMotion = true)
}

/** Native's look before one is set (`geode_viz_look_defaults`), read once. */
private object NativeLookDefaults {
    private val lines = GeodeNative.vizLookDefaults().lines()
    val sceneId: String = lines[0]
    val transitionId: String = lines[1]
    val transitionMs: Long = lines[2].toLong()
}

/**
 * A background image ready for a renderer's underlay. A plain class, not a data class: [pixels] is a large
 * array that the generated equals() would compare by reference anyway, which is misleading.
 */
class LookUnderlay(
    val pixels: IntArray,
    val width: Int,
    val height: Int,
    val blend: Int,
    val amount: Float,
)

/** Where a host's look comes from: the saved stores (wallpaper, dream, Cast) or the session's state (export). */
fun interface LookSource {
    fun current(): LookSnapshot

    /** The look as it is now, frozen for a render: later store or session changes never reach it. */
    fun pin(): LookSnapshot = current()
}
