package dev.geode.render.look

import dev.geode.engine.bridge.GeodeNative
import dev.geode.render.AdsrConfig
import dev.geode.render.LfoConfig
import dev.geode.render.ReactionRoute
import dev.geode.render.scene.SceneParams

/**
 * The seam a [LookSnapshot] is applied through: one native renderer's look setters. `NativeViz` is the real
 * adapter (the live `VisualizerRenderer` and `OffscreenSceneRenderer` each drive one); JVM tests use a fake.
 * Signatures are NativeViz's own, so the adapter is nothing but `override`.
 */
interface LookTarget {
    /** False when the renderer cannot draw [sceneId] yet; [LookApplier] asks again next time. */
    fun setScene(sceneId: String): Boolean

    fun setParams(p: SceneParams)

    fun setLfoConfigs(configs: List<LfoConfig>)

    fun setAdsrConfigs(configs: List<AdsrConfig>)

    fun setUserReactions(routes: List<ReactionRoute>?)

    fun setSoundTuning(floats: FloatArray)

    fun setReducedMotion(on: Boolean)

    fun setTransition(
        id: String,
        durationMs: Long,
    )

    /** Null [pixels] clears the underlay. */
    fun setUnderlay(
        pixels: IntArray?,
        width: Int,
        height: Int,
        blend: Int,
        amount: Float,
    )
}

/**
 * Pushes a [LookSnapshot] into one [LookTarget], only what changed since the last [apply]: the live renderer
 * applies every frame, so an unchanged look must cost nothing, and a slider drag must push params alone.
 * Not thread-safe: one applier per target, called on that target's GL thread. [reset] when the target
 * is new (a new surface or native handle), so the next [apply] sends the whole look.
 *
 * The live renderer passes the frame's `due` bits from native (`GeodeNative.FRAME_PUSH_*`, core/viz/frame/
 * PushVersions): native owns when the params, LFOs, envelopes and tuning cross, so those four go exactly
 * when their bit is set. With no `due` (an export, tests) each goes when it differs from the last applied.
 */
class LookApplier {
    private var applied: LookSnapshot? = null
    private var appliedScene: String? = null

    fun reset() {
        applied = null
        appliedScene = null
    }

    fun apply(
        look: LookSnapshot,
        target: LookTarget,
        due: Int? = null,
    ) {
        if (look.sceneId != appliedScene && target.setScene(look.sceneId)) appliedScene = look.sceneId
        val was = applied
        if (due == null && was === look) return
        applyPushed(look, was, target, due)
        applyRest(look, was, target)
        applied = look
    }

    // The four inputs native's cadence owns (see the class comment).
    private fun applyPushed(
        look: LookSnapshot,
        was: LookSnapshot?,
        target: LookTarget,
        due: Int?,
    ) {
        fun pushes(
            bit: Int,
            changed: Boolean,
        ) = if (due == null) was == null || changed else due and bit != 0
        if (pushes(GeodeNative.FRAME_PUSH_PARAMS, look.params !== was?.params)) target.setParams(look.params)
        if (pushes(GeodeNative.FRAME_PUSH_LFO, look.lfos !== was?.lfos)) target.setLfoConfigs(look.lfos)
        if (pushes(GeodeNative.FRAME_PUSH_ADSR, look.adsrs !== was?.adsrs)) target.setAdsrConfigs(look.adsrs)
        if (pushes(GeodeNative.FRAME_PUSH_TUNING, look.soundTuning !== was?.soundTuning)) target.setSoundTuning(look.soundTuning.toFloats())
    }

    private fun applyRest(
        look: LookSnapshot,
        was: LookSnapshot?,
        target: LookTarget,
    ) {
        if (was == null || look.reactions !== was.reactions) target.setUserReactions(look.reactions)
        if (was == null || look.reducedMotion != was.reducedMotion) target.setReducedMotion(look.reducedMotion)
        if (was == null || look.transitionId != was.transitionId || look.transitionMs != was.transitionMs) {
            target.setTransition(look.transitionId, look.transitionMs)
        }
        if (was == null || look.underlay !== was.underlay) {
            val u = look.underlay
            target.setUnderlay(u?.pixels, u?.width ?: 0, u?.height ?: 0, u?.blend ?: 0, u?.amount ?: 0f)
        }
    }
}
