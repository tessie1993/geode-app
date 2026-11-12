package dev.geode.render.look

import dev.geode.engine.audio.SoundTuning
import dev.geode.render.AdsrConfig
import dev.geode.render.AdsrEngine
import dev.geode.render.LfoConfig
import dev.geode.render.LfoEngine
import dev.geode.render.ReactionRoute
import dev.geode.render.TransitionStyle
import dev.geode.render.scene.SceneParams

/**
 * The look snapshot: everything the user set that decides what a renderer draws, as one immutable value
 * (#345). Not the auto-director's Look (GLOSSARY), which is one decision made inside the engine.
 *
 * Every renderer host (live Visuals, the wallpaper, the dream, Cast, an export) only says where its look
 * comes from ([LookSource]) and what it overrides ([gentleFlight]); `NativeViz.setLook` pushes it into the
 * native renderer. Before this, each host rebuilt it field by field and they drifted: the wallpaper and
 * dream once drew with every LFO, envelope and custom reaction off.
 *
 * Being a value is what pins an export: a render takes one look when it starts, and later edits never reach
 * it. Parts are compared by identity when applied, so a change is a new instance, as the stores make them.
 *
 * Not here, being process-wide native state: the macro bank, Note & key, the director and the hand mask
 * (`EngineSettings`, pinned natively by an export), the layer (#602) and custom shader code (#498).
 */
data class LookSnapshot(
    /** Until a look is pushed, the app's first style (FreeTier.FIRST_STYLE_ID), not a Pro one. */
    val sceneId: String = "neon_chrome_sea",
    val params: SceneParams = SceneParams.DEFAULT,
    val lfos: List<LfoConfig> = List(LfoEngine.SLOTS) { LfoConfig() },
    val adsrs: List<AdsrConfig> = List(AdsrEngine.COUNT) { AdsrConfig() },
    /** The user's own reaction routes, or null to play the style's DNA (native merges under the hand mask). */
    val reactions: List<ReactionRoute>? = null,
    val soundTuning: SoundTuning = SoundTuning(),
    /** Vestibular accessibility, and the dream's gentle flight; the flash clamp is native and unconditional. */
    val reducedMotion: Boolean = false,
    /** A requested cut becomes a fade natively (core/viz/VisualSafety); an offscreen renderer cuts for real, natively. */
    val transitionId: String = TransitionStyle.FADE.name.lowercase(),
    val transitionMs: Long = DEFAULT_TRANSITION_MS,
    val underlay: LookUnderlay? = null,
) {
    /** The dream's override: the camera keeps flying without big moves, because it is watched from across a room. */
    fun gentleFlight(): LookSnapshot = copy(reducedMotion = true)

    private companion object {
        const val DEFAULT_TRANSITION_MS = 1200L
    }
}

/**
 * A background image ready for `NativeViz.setUnderlay`. A plain class, not a data class: [pixels] is a large
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
