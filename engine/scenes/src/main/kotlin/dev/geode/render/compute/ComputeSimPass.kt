package dev.geode.render.compute

import android.util.Log
import dev.geode.engine.gl.ComputePass
import dev.geode.engine.gl.ImageAccess
import dev.geode.engine.gl.WorkGroupCount
import dev.geode.render.scene.GlUtil

/**
 * The ES 3.1 path: a step is a dispatch.
 *
 * What is *not* here is the point of the file. There is no fullscreen triangle, no vertex array,
 * no framebuffer bind, no viewport save and restore, and no tile resolve — the whole
 * rasterization apparatus the fragment path needs to arrange for one invocation per texel is
 * replaced by asking for exactly that many invocations.
 *
 * The read state is bound as a **sampler**, not as a second image, so the body's `simSample`
 * gets hardware interpolation on formats that prove filterable. Only the destination is an
 * image, and it is `writeonly`: nothing in a step reads the texture it is writing, which is
 * what keeps the dispatch race-free without any ordering between work groups.
 */
internal class ComputeSimPass(
    label: String,
    encoding: SimStateEncoding,
    state: SimField,
    private val pass: ComputePass,
    private val maxGroups: WorkGroupCount,
) : BaseSimPass(label, encoding, state) {
    override val pathLabel: String get() = "compute"

    private val uniforms =
        SimUniforms(GlUtil.UniformCache(pass.programName)) { unit, texture ->
            // Routed through the pass so its end() unbinds it. Allocated once, here, not per
            // frame at the step site.
            pass.texture(unit, texture)
        }

    /**
     * Cached dispatch dimensions. Recomputed only when the grid changes: `groupsFor` allocates,
     * and a step runs every frame.
     */
    private var groups = WorkGroupCount(x = 0, y = 0, z = 0)
    private var groupsWidth = -1
    private var groupsHeight = -1

    override fun step(binder: SimUniformBinder): Boolean {
        if (!ensureStorage()) return false
        if (!ensureGroups()) return false

        pass.begin()
        pass.texture(SimGlsl.STATE_TEXTURE_UNIT, state.readTexture)
        // Image unit 0 and texture unit 0 are different namespaces; both being zero here is
        // correct, not a collision.
        pass.image(SimGlsl.STATE_IMAGE_UNIT, state.writeTexture, encoding.format, ImageAccess.WRITE)
        uniforms.int(SimGlsl.UNIFORM_STATE, SimGlsl.STATE_TEXTURE_UNIT)
        uniforms.ivec2(SimGlsl.UNIFORM_SIZE, state.width, state.height)
        binder.bind(uniforms)
        // dispatch() issues the memory barrier immediately afterwards. Nothing between the
        // dispatch and the barrier, and nothing that reads the result before it.
        pass.dispatch(groups)
        pass.end()
        state.swap()
        return true
    }

    override fun release() {
        pass.release()
        super.release()
    }

    override fun forget() {
        pass.forget()
        super.forget()
    }

    private fun ensureGroups(): Boolean {
        if (state.width == groupsWidth && state.height == groupsHeight) return groups.x > 0
        groups = pass.localSize.groupsFor(state.width, state.height)
        groupsWidth = state.width
        groupsHeight = state.height
        // Unreachable on any grid this app dispatches — at 8x8 the x limit's spec floor of
        // 65535 groups is a 524280-texel-wide simulation. Checked anyway because the failure
        // mode without it is GL_INVALID_VALUE and a dispatch that silently does nothing, and an
        // error flag nobody reads is the most expensive kind of bug this layer can produce.
        if (groups.x > maxGroups.x || groups.y > maxGroups.y || groups.z > maxGroups.z) {
            Log.w(
                TAG,
                "$label: ${state.width}x${state.height} needs ${groups.x}x${groups.y} work groups, " +
                    "over this device's ${maxGroups.x}x${maxGroups.y} limit",
            )
            groups = WorkGroupCount(x = 0, y = 0, z = 0)
            return false
        }
        return true
    }

    private companion object {
        const val TAG = "ComputeSimPass"
    }
}
