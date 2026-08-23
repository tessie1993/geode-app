package dev.geode.render.compute

import android.opengl.GLES30
import dev.geode.render.scene.GlUtil

/**
 * The ES 3.0 path: a step is a fullscreen triangle over the write target.
 *
 * This is the baseline and it stays the baseline forever — not a degraded mode. It is also the
 * only path a device without proven compute will ever run, so it gets the same care: the
 * caller's framebuffer and viewport are saved and restored, and the write target's previous
 * contents are explicitly discarded before the draw.
 *
 * ### The invalidate
 *
 * On a tiled GPU, binding a framebuffer whose contents will be read back into the tile costs a
 * full load of that attachment from main memory before the first fragment, and the step never
 * reads it — every texel is written from the *other* side of the ping-pong. Telling the driver
 * so with `glInvalidateFramebuffer` removes the load. This is the "budget bandwidth, not ALU"
 * rule in its smallest form, and it is only valid because the step is a total function of the
 * read state; a step that updated part of the grid would read back undefined texels.
 */
internal class FragmentSimPass(
    label: String,
    encoding: SimStateEncoding,
    state: SimField,
    program: Int,
) : BaseSimPass(label, encoding, state) {
    override val pathLabel: String get() = "fragment"

    /** Zeroed by [release] and [forget] so a second call cannot delete a name GL has reissued. */
    private var program: Int = program

    private val locations = GlUtil.UniformCache(program)

    private var sceneTextureUnits = 0

    private val uniforms =
        SimUniforms(locations) { unit, texture ->
            GLES30.glActiveTexture(GLES30.GL_TEXTURE0 + unit)
            GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, texture)
            if (unit < TRACKED_UNITS) sceneTextureUnits = sceneTextureUnits or (1 shl unit)
        }

    private var vao = 0

    // Reused every frame by design: this is the render hot path, and a four-element array
    // allocated inside the frame loop is exactly what the hot-path exception exists to avoid.
    private val previousFramebuffer = IntArray(1)
    private val previousViewport = IntArray(VIEWPORT_COMPONENTS)
    private val invalidatedAttachments = intArrayOf(GLES30.GL_COLOR_ATTACHMENT0)

    override fun step(binder: SimUniformBinder): Boolean {
        if (!ensureStorage()) return false
        ensureVertexArray()

        GLES30.glGetIntegerv(GLES30.GL_DRAW_FRAMEBUFFER_BINDING, previousFramebuffer, 0)
        GLES30.glGetIntegerv(GLES30.GL_VIEWPORT, previousViewport, 0)

        // GL_DRAW_FRAMEBUFFER rather than GL_FRAMEBUFFER: binding GL_FRAMEBUFFER would move
        // the caller's read target as well, and only the draw target is ours to move.
        GLES30.glBindFramebuffer(GLES30.GL_DRAW_FRAMEBUFFER, state.writeFramebuffer)
        GLES30.glViewport(0, 0, state.width, state.height)
        GLES30.glInvalidateFramebuffer(GLES30.GL_DRAW_FRAMEBUFFER, 1, invalidatedAttachments, 0)
        // Blending is disabled rather than restored, matching every hand-written step pass in
        // the tree; GlUtil.resetFrameState owns per-frame state and the scene's display pass
        // sets whatever it needs. For the packed encoding this is not merely tidy: an integer
        // colour attachment has no blending at all, and leaving it enabled is undefined.
        GLES30.glDisable(GLES30.GL_BLEND)
        GLES30.glDisable(GLES30.GL_DEPTH_TEST)

        GLES30.glUseProgram(program)
        GLES30.glActiveTexture(GLES30.GL_TEXTURE0 + SimGlsl.STATE_TEXTURE_UNIT)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, state.readTexture)
        uniforms.int(SimGlsl.UNIFORM_STATE, SimGlsl.STATE_TEXTURE_UNIT)
        uniforms.ivec2(SimGlsl.UNIFORM_SIZE, state.width, state.height)
        binder.bind(uniforms)

        GLES30.glBindVertexArray(vao)
        GLES30.glDrawArrays(GLES30.GL_TRIANGLES, 0, TRIANGLE_VERTICES)
        GLES30.glBindVertexArray(0)

        unbindTextures()
        GLES30.glUseProgram(0)
        GLES30.glBindFramebuffer(GLES30.GL_DRAW_FRAMEBUFFER, previousFramebuffer[0])
        GLES30.glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3])
        state.swap()
        return true
    }

    override fun release() {
        if (program != 0) GLES30.glDeleteProgram(program)
        if (vao != 0) GLES30.glDeleteVertexArrays(1, intArrayOf(vao), 0)
        program = 0
        vao = 0
        super.release()
    }

    override fun forget() {
        program = 0
        vao = 0
        super.forget()
    }

    /**
     * An empty vertex array, not `GlUtil.FullscreenTriangle`. The generated vertex stage builds
     * its position from `gl_VertexID` and reads no attributes at all, so a buffer and an
     * enabled attribute array would be two objects that exist only to be ignored — and an
     * enabled attribute with no data behind it is its own class of driver complaint.
     */
    private fun ensureVertexArray() {
        if (vao != 0) return
        val ids = IntArray(1)
        GLES30.glGenVertexArrays(1, ids, 0)
        vao = ids[0]
    }

    private fun unbindTextures() {
        GLES30.glActiveTexture(GLES30.GL_TEXTURE0 + SimGlsl.STATE_TEXTURE_UNIT)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, 0)
        for (unit in 0 until TRACKED_UNITS) {
            if (sceneTextureUnits and (1 shl unit) == 0) continue
            GLES30.glActiveTexture(GLES30.GL_TEXTURE0 + unit)
            GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, 0)
        }
        sceneTextureUnits = 0
        GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
    }

    private companion object {
        const val TRIANGLE_VERTICES = 3
        const val VIEWPORT_COMPONENTS = 4
        const val TRACKED_UNITS = 32
    }
}
