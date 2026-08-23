package dev.geode.render.scene

import android.opengl.GLES30
import dev.geode.render.TouchField
import dev.geode.render.compute.SimUniforms

/**
 * How the fingers reach a scene that owns its own shader programs.
 *
 * WHY THIS EXISTS: [ShaderScene] is handed the [TouchField] by `SceneRegistry`
 * and uploads the whole touch block that `lib_scene_uniforms` declares, so all
 * 27 fragment styles read the fingers through one include. The families that
 * are NOT fragment styles — cymatics, the four field sims, the beam — each own
 * their own programs and their own hand-written uniform blocks, and none of
 * them can include that library (they are not `view()`-shaped, and several are
 * multi-pass or vertex-side). They all want the same two values out of the
 * field, though, and six hand-rolled copies of the same upload is six chances
 * for the packing to drift silently. This is the one copy.
 *
 * ## The shader-side contract
 *
 * A program that reads the fingers declares exactly this, and nothing else:
 *
 * ```glsl
 * #define TOUCH_MAX_POINTS 5
 * uniform vec4 uTouchPoints[TOUCH_MAX_POINTS]; // xy position, z strength, w age
 * uniform int uTouchCount;
 * ```
 *
 * `xy` is **y-up NDC**: -1..1 with the origin at the centre of the surface and
 * +y toward the top, aspect NOT pre-applied — the identical convention the
 * fragment styles read, so one packing means one thing everywhere in the app.
 * A shader that wants square units multiplies x by its own aspect.
 *
 * `z` is 0..1: 1 while the finger is down, decaying over
 * [TouchField.RELEASE_TAU_SECONDS] after it lifts. `w` is the age in seconds.
 * Slot 0 is the primary finger.
 *
 * ## The one guarantee
 *
 * `uTouchCount == 0` is the exact idle test, and every family's touch code is
 * behind it. [TouchField.publish] sets `count` from the highest slot with a
 * non-zero strength, so it is 0 both before anything is ever touched and again
 * once the last release wake has decayed out — the same instant the anchor is
 * spent, since both use the same tau and the same spent threshold. So an
 * untouched frame renders bit-identically to one from before any of this
 * existed, which is what makes it safe to wire touch into families that are
 * already somebody's saved preset.
 *
 * [upload] therefore writes the point array only when there is something to
 * say. A stale array left behind by an earlier gesture is unreachable: every
 * loop that reads it breaks on `uTouchCount` first.
 */
internal object SceneTouch {
    /** Slots the shaders declare. Mirrors [TouchField.MAX_POINTS]; changing one alone breaks the upload. */
    const val MAX_POINTS: Int = TouchField.MAX_POINTS

    /**
     * Upload the touch block to the currently bound program.
     *
     * [enabled] is for the passes that must see the fingers only once per
     * frame — [LifeScene] steps its solver up to eight times per frame and
     * gates every injection to the first substep, or a touch would be
     * integrated eight times over and hit the state that much harder at high
     * Speed than at low. Passing `false` publishes the untouched state, which
     * is the same early-out the rest of the frame takes.
     */
    fun upload(
        locs: GlUtil.UniformCache,
        field: TouchField?,
        enabled: Boolean = true,
    ) {
        val count = if (enabled && field != null) field.count else 0
        GLES30.glUniform1i(locs.loc("uTouchCount"), count)
        if (count <= 0 || field == null) return
        GLES30.glUniform4fv(
            locs.loc("uTouchPoints"),
            locs.arrayCount("uTouchPoints", MAX_POINTS),
            field.points,
            0,
        )
    }

    /**
     * The same upload, for a step running through `SimPass`.
     *
     * An overload rather than a second implementation: [SimUniforms] exists so that a step body
     * cannot pick its own texture unit and cannot see the encoding, so it does not hand out the
     * program name and there is no `UniformCache` here to pass to the call above. What it does
     * expose is the same two setters this needs, and routing through them keeps the packing —
     * y-up NDC, `z` the strength, `w` the age, slot 0 the primary finger — described in exactly
     * one place for the compute path and the fragment path alike.
     */
    fun upload(
        uniforms: SimUniforms,
        field: TouchField?,
        enabled: Boolean = true,
    ) {
        val count = if (enabled && field != null) field.count else 0
        uniforms.int("uTouchCount", count)
        if (count <= 0 || field == null) return
        uniforms.vec4Array("uTouchPoints", field.points, count, MAX_POINTS)
    }
}

/**
 * A scene that reads the fingers itself rather than having a ripple laid over it.
 *
 * `OverlayEffects.drainTouchStrokes` hands the field to whichever scene is on
 * screen. That is the same handover `SceneRegistry.setTouchField` performs for
 * the fragment styles, at the one place in the render loop that already knows
 * both the live scene and where the fingers are.
 */
internal interface TouchReactive {
    /** Hand this scene the touch field it should read from now on. Idempotent. */
    fun setTouchField(field: TouchField)
}
