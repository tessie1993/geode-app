package dev.geode.data

import dev.geode.engine.bridge.VizNative
import dev.geode.model.SceneParams

/**
 * The look snapshot: everything the user set that decides what a renderer draws, as one immutable value
 * (#345). Not the auto-director's Look (GLOSSARY), which is one decision made inside the engine.
 *
 * Every renderer host (live Visuals, the dream, Cast, an export) only says where its look
 * comes from ([LookSource]) and what it overrides ([gentleFlight]); the host hands it whole to native
 * (`geode_viz_set_look`, `geode_stage_set_look`), which passes on only the parts that changed. Before this,
 * each host rebuilt it field by field and they drifted: the wallpaper and dream once drew with every LFO,
 * envelope and custom reaction off.
 *
 * Being a value is what pins an export: a render takes one look when it starts, and later edits never reach
 * it. The waves, the envelopes and the routes are their saved texts, read by native alone.
 *
 * Not here, being process-wide native state: the macro bank, the director and the hand mask
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

/**
 * Native's look before one is set and the shell's visual defaults (`geode_viz_look_defaults`,
 * core/viz/LookSnapshot.hpp), read once. JVM tests cannot load libgeode, so they [install] text in native's shape.
 */
object NativeLookDefaults {
    @Volatile
    private var read: () -> String = { VizNative.vizLookDefaults() }

    @Volatile
    private var lines: List<String>? = null

    fun install(next: () -> String) {
        read = next
        lines = null
    }

    private fun line(i: Int): String = (lines ?: read().lines().also { lines = it })[i]

    val sceneId: String get() = line(0)
    val transitionId: String get() = line(1)
    val transitionMs: Long get() = line(2).toLong()
    val attack: Float get() = line(3).toFloat()
    val decay: Float get() = line(4).toFloat()
    val styleTimerSec: Int get() = line(5).toInt()
    val playlistTimerSec: Int get() = line(6).toInt()
    val presetMorphSecondsMax: Float get() = line(7).toFloat()
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

/** Where a host's look comes from: the saved stores (the dream, Cast) or the session's state (export). */
fun interface LookSource {
    fun current(): LookSnapshot

    /** The look as it is now, frozen for a render: later store or session changes never reach it. */
    fun pin(): LookSnapshot = current()
}
