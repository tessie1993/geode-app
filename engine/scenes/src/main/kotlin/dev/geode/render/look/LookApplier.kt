package dev.geode.render.look

import dev.geode.engine.bridge.GeodeNative
import dev.geode.render.scene.SceneParams
import dev.geode.util.bestEffort

/**
 * The seam a [LookSnapshot] is applied through: one native renderer's look setters. `NativeViz` is the real
 * adapter (signatures are its own, so it is nothing but `override`); JVM tests use a fake.
 */
interface LookTarget {
    /** False when the renderer cannot draw [sceneId] yet; [LookApplier] asks again next time. */
    fun setScene(sceneId: String): Boolean

    fun setParams(p: SceneParams)

    fun setLfoConfigs(lfos: String)

    fun setAdsrConfigs(adsrs: String)

    /** The routes with the hand mask native merges them under; the adapter sends only a change of either. */
    fun setUserReactions(
        routes: String?,
        mask: Int,
    )

    fun setSoundTuning(tuning: dev.geode.engine.audio.SoundTuning)

    fun setReducedMotion(on: Boolean)

    fun setTransition(
        id: String,
        durationMs: Long,
    )

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
 * applies every frame, so an unchanged look must cost nothing. One applier per target, on that target's GL
 * thread; [reset] when the target is new (a new surface or handle), so the next [apply] sends the whole look.
 *
 * The live renderer passes native's `due` bits (`GeodeNative.FRAME_PUSH_*`, core/viz/frame/PushVersions): the
 * params, LFOs, envelopes and tuning go exactly on their bit. With no `due` (an export, tests) each goes on a
 * change. The hand mask is not in the look (process-wide, or an export's pin): the reactions go with it.
 */
class LookApplier {
    private var applied: LookSnapshot? = null
    private var appliedScene: String? = null
    private var appliedMask: Int? = null

    fun reset() {
        applied = null
        appliedScene = null
        appliedMask = null
    }

    fun apply(
        look: LookSnapshot,
        target: LookTarget,
        due: Int? = null,
        handMask: Int = HandMask.current,
    ) {
        if (look.sceneId != appliedScene && target.setScene(look.sceneId)) appliedScene = look.sceneId
        val was = applied

        fun pushes(
            bit: Int,
            changed: Boolean,
        ) = if (due == null) was == null || changed else due and bit != 0
        if (pushes(GeodeNative.FRAME_PUSH_PARAMS, look.params !== was?.params)) target.setParams(look.params)
        if (pushes(GeodeNative.FRAME_PUSH_LFO, look.lfos != was?.lfos)) target.setLfoConfigs(look.lfos)
        if (pushes(GeodeNative.FRAME_PUSH_ADSR, look.adsrs != was?.adsrs)) target.setAdsrConfigs(look.adsrs)
        if (pushes(GeodeNative.FRAME_PUSH_TUNING, look.soundTuning !== was?.soundTuning)) target.setSoundTuning(look.soundTuning)
        if (was == null || look.reactions != was.reactions || handMask != appliedMask) target.setUserReactions(look.reactions, handMask)
        if (was == null || look.reducedMotion != was.reducedMotion) target.setReducedMotion(look.reducedMotion)
        if (was == null || look.transitionId != was.transitionId || look.transitionMs != was.transitionMs) {
            target.setTransition(look.transitionId, look.transitionMs)
        }
        if (was == null || look.underlay !== was.underlay) {
            val u = look.underlay
            target.setUnderlay(u?.pixels, u?.width ?: 0, u?.height ?: 0, u?.blend ?: 0, u?.amount ?: 0f)
        }
        appliedMask = handMask
        applied = look
    }
}

/**
 * Customize › Music's hand mask for this process, the value every renderer merges with (pushed with the
 * user's routes; core/viz/ReactionResolve) and every native director keeps off (`hand::global`), set where the
 * shell loads the user's routes. 0 until then, as native starts; native counts it only while [set] says a list
 * exists (#355).
 */
object HandMask {
    @Volatile
    var current: Int = 0
        private set

    fun set(
        mask: Int,
        hasUser: Boolean,
    ) {
        current = mask
        bestEffort("HandMask", "push hand mask") { GeodeNative.handSet(mask, hasUser) }
    }
}
