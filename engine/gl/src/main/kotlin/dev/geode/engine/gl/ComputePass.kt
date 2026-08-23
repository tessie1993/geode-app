package dev.geode.engine.gl

import android.opengl.GLES30
import android.opengl.GLES31
import android.util.Log

/**
 * What will read a compute shader's writes *next*.
 *
 * ### The bits name the reader, not the writer
 *
 * This is the single most-missed thing about `glMemoryBarrier`, and it is why this is an enum
 * of readers rather than a raw mask. The bit set does not describe how the shader wrote —
 * "I used `imageStore`, so I pass `GL_SHADER_IMAGE_ACCESS_BARRIER_BIT`" is wrong and produces a
 * race that survives the fix. The spec defines the bits as the set of operations **issued after
 * the barrier** that will observe writes issued before it. A step that writes with `imageStore`
 * and is then sampled with `texture()` needs [TEXTURE_SAMPLE], not [IMAGE_LOAD_STORE].
 *
 * ### What a barrier does not do
 *
 * It does not order invocations *within* a dispatch. Work groups have no ordering relative to
 * each other and never will; a step that reads a texel another invocation writes is a data race
 * that no barrier repairs, which is why simulation state ping-pongs between two textures
 * instead of being updated in place. `glMemoryBarrier` is only ever about the *next command*
 * seeing what the last one wrote.
 */
enum class ComputeReader(
    val barrierBit: Int,
) {
    /**
     * Anything sampled — `texture`, `textureLod`, `texelFetch` — in **any** stage, the vertex
     * stage included. Vertex *texture* fetch is a texture fetch; the SwissGL pattern of reading
     * particle state in the vertex shader lands here, not on [VERTEX_ATTRIBUTES]. Choosing
     * `GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT` for that case is the classic wrong answer, and it
     * fails intermittently on exactly the tilers where it matters.
     */
    TEXTURE_SAMPLE(GLES31.GL_TEXTURE_FETCH_BARRIER_BIT),

    /** `imageLoad` / `imageStore` / image atomics in a later shader. */
    IMAGE_LOAD_STORE(GLES31.GL_SHADER_IMAGE_ACCESS_BARRIER_BIT),

    /** `buffer` block reads and writes in a later shader. */
    SHADER_STORAGE(GLES31.GL_SHADER_STORAGE_BARRIER_BIT),

    /** Vertex attributes sourced from a buffer the step wrote. Buffer-backed attributes only. */
    VERTEX_ATTRIBUTES(GLES31.GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT),

    /** Index data for an indexed draw, sourced from a buffer the step wrote. */
    ELEMENT_INDICES(GLES31.GL_ELEMENT_ARRAY_BARRIER_BIT),

    /** Uniform block reads. */
    UNIFORM_BLOCK(GLES31.GL_UNIFORM_BARRIER_BIT),

    /**
     * `glDrawArraysIndirect` / `glDispatchComputeIndirect` reading their arguments from a
     * buffer the step wrote — a compute pass deciding its own next dispatch size.
     */
    INDIRECT_COMMAND(GLES31.GL_COMMAND_BARRIER_BIT),

    /**
     * Fixed-function framebuffer access to the written texture: as a colour attachment, as a
     * blit source or destination, or through `glReadPixels`.
     */
    FRAMEBUFFER_ATTACHMENT(GLES31.GL_FRAMEBUFFER_BARRIER_BIT),

    /** `glTexSubImage` / `glCopyTexSubImage` / `glGetTexImage`-style texture updates. */
    TEXTURE_UPDATE(GLES31.GL_TEXTURE_UPDATE_BARRIER_BIT),

    /** `glBufferSubData` / `glMapBufferRange` on a buffer the step wrote. */
    BUFFER_UPDATE(GLES31.GL_BUFFER_UPDATE_BARRIER_BIT),

    /** Pixel pack/unpack buffer transfers. */
    PIXEL_TRANSFER(GLES31.GL_PIXEL_BUFFER_BARRIER_BIT),

    /** Atomic counter reads. */
    ATOMIC_COUNTER(GLES31.GL_ATOMIC_COUNTER_BARRIER_BIT),

    /** Transform feedback writes. */
    TRANSFORM_FEEDBACK(GLES31.GL_TRANSFORM_FEEDBACK_BARRIER_BIT),
    ;

    companion object {
        /** ORs a reader set into the mask `glMemoryBarrier` takes. Do this once, not per frame. */
        fun maskOf(readers: Set<ComputeReader>): Int = readers.fold(0) { mask, reader -> mask or reader.barrierBit }
    }
}

/**
 * One compute dispatch: bind, dispatch, barrier, unbind.
 *
 * Owns its [ComputeProgram] — [release] deletes it — because a program and the barrier mask its
 * output requires are two halves of one decision, and separating them is how a program ends up
 * dispatched with somebody else's barrier.
 *
 * ### Ordering inside [dispatch]
 *
 * `glDispatchCompute` then `glMemoryBarrier`, in that order, always. Issuing the barrier
 * *before* the dispatch is a real and popular bug: it compiles, it runs, and it orders the
 * **previous** dispatch's writes while leaving this one's unordered. The failure is
 * intermittent, load-dependent, and reads exactly like a driver bug.
 *
 * `glMemoryBarrierByRegion` is deliberately not used. It is cheaper, but it only orders
 * accesses within the same framebuffer region — a concept a compute dispatch does not have,
 * since it is not attached to a framebuffer. For compute-to-anything the full barrier is the
 * only correct one.
 *
 * ### Hot path
 *
 * Everything here is called once per simulation step, every frame. It allocates nothing: the
 * barrier mask is folded at construction and the bound-unit bookkeeping is two `Int` bitmasks.
 * That is the deliberate hot-path exception the repo documents, not an oversight.
 */
class ComputePass(
    private val label: String,
    private val program: ComputeProgram,
    readers: Set<ComputeReader>,
) {
    private val barrierMask = ComputeReader.maskOf(readers)

    /**
     * Which units this pass touched, so [end] can clear exactly those. Bitmasks rather than
     * lists because they are free to reset and allocate nothing; 32 units is well past
     * `GL_MAX_COMPUTE_IMAGE_UNIFORMS` (spec floor 4) and `GL_MAX_COMPUTE_TEXTURE_IMAGE_UNITS`
     * (spec floor 16) on any device that will ever run this.
     */
    private var imageUnits = 0
    private var textureUnits = 0
    private var storageBindings = 0

    /** The local size the program was linked with; the caller needs it to size the dispatch. */
    val localSize: WorkGroupSize get() = program.localSize

    /**
     * The linked program name, for a caller that keeps its own uniform-location cache. Exposed
     * rather than proxying every `glUniform*` through this class: a caller setting a dozen
     * uniforms a frame wants its locations cached once, and this class is not that cache.
     */
    val programName: Int get() = program.program

    init {
        if (barrierMask == 0) {
            // Not fatal — a step whose result genuinely nothing reads is expressible — but it
            // is far more often a forgotten reader, and that bug is invisible until it is not.
            Log.w(TAG, "$label declares no readers, so no memory barrier will be issued after its dispatch")
        }
    }

    /** Makes the program current. Bindings must follow, not precede. */
    fun begin() {
        program.use()
    }

    /** Uniform location on this pass's program, or -1. */
    fun uniformLocation(name: String): Int = program.uniformLocation(name)

    /**
     * Binds a texture as an image at [unit].
     *
     * [format] must be the texture's own internal format and must equal the shader's
     * `layout(...)` qualifier — see [GlImageFormat] for why all three have to agree and what
     * happens when they do not.
     *
     * The texture must have been allocated with `glTexStorage2D`. ES 3.1 raises
     * `GL_INVALID_OPERATION` for `glBindImageTexture` on a mutable texture, which is a silent
     * no-op dispatch for anyone who allocated with `glTexImage2D` out of habit.
     */
    fun image(
        unit: Int,
        texture: Int,
        format: GlImageFormat,
        access: ImageAccess,
    ) {
        GLES31.glBindImageTexture(unit, texture, 0, false, 0, access.glAccess, format.internalFormat)
        imageUnits = imageUnits or bit(unit)
    }

    /** Binds a texture for sampling at [unit]. */
    fun texture(
        unit: Int,
        texture: Int,
    ) {
        GLES30.glActiveTexture(GLES30.GL_TEXTURE0 + unit)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, texture)
        textureUnits = textureUnits or bit(unit)
    }

    /** Binds a buffer as an SSBO at binding point [index]. */
    fun storageBuffer(
        index: Int,
        buffer: Int,
    ) {
        GLES30.glBindBufferBase(GLES31.GL_SHADER_STORAGE_BUFFER, index, buffer)
        storageBindings = storageBindings or bit(index)
    }

    /**
     * Dispatches [groups] work groups and issues the barrier for the declared readers.
     *
     * A zero in any axis is a no-op dispatch in the spec, but it is also always a bug in the
     * caller's group-count arithmetic, so it is refused and logged rather than swallowed.
     */
    fun dispatch(groups: WorkGroupCount) {
        if (groups.x <= 0 || groups.y <= 0 || groups.z <= 0) {
            Log.w(TAG, "$label: refusing a dispatch of ${groups.x}x${groups.y}x${groups.z} work groups")
            return
        }
        GLES31.glDispatchCompute(groups.x, groups.y, groups.z)
        if (barrierMask != 0) GLES31.glMemoryBarrier(barrierMask)
    }

    /**
     * Clears every binding this pass made and unbinds the program.
     *
     * Worth the handful of calls: an image binding left in place keeps referencing a texture
     * the ping-pong is about to write through the other alias, and a stale binding is precisely
     * the kind of state that makes the *next* pass fail on one driver and not another. Same
     * discipline the probe pass follows — leave no trace.
     */
    fun end() {
        forEachBit(imageUnits) { unit ->
            // Texture 0 with a legal format: the format argument is validated even when the
            // texture is zero, so passing 0 there raises GL_INVALID_VALUE on strict drivers.
            GLES31.glBindImageTexture(unit, 0, 0, false, 0, GLES31.GL_READ_ONLY, GLES30.GL_RGBA8)
        }
        forEachBit(textureUnits) { unit ->
            GLES30.glActiveTexture(GLES30.GL_TEXTURE0 + unit)
            GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, 0)
        }
        forEachBit(storageBindings) { index ->
            GLES30.glBindBufferBase(GLES31.GL_SHADER_STORAGE_BUFFER, index, 0)
        }
        GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
        GLES30.glUseProgram(0)
        imageUnits = 0
        textureUnits = 0
        storageBindings = 0
    }

    /** Deletes the program. Requires a live context. */
    fun release() {
        program.release()
    }

    /** Drops the program handle without calling GL, for a context that is already gone. */
    fun forget() {
        program.forget()
    }

    private companion object {
        const val TAG = "ComputePass"

        const val TRACKED_UNITS = 32

        fun bit(unit: Int): Int = if (unit in 0 until TRACKED_UNITS) 1 shl unit else 0

        inline fun forEachBit(
            mask: Int,
            action: (Int) -> Unit,
        ) {
            for (unit in 0 until TRACKED_UNITS) {
                if (mask and (1 shl unit) != 0) action(unit)
            }
        }
    }
}
