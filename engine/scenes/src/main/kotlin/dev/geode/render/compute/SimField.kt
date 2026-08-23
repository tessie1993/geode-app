package dev.geode.render.compute

import android.opengl.GLES30
import android.util.Log
import dev.geode.engine.gl.GlImageFormat

/**
 * The ping-pong pair a simulation's state lives in.
 *
 * ### Why ping-pong survives the compute tier
 *
 * The obvious thing to try on ES 3.1 is to drop the second texture and update state in place
 * with a read-write image. It does not work and it fails silently: invocations in different
 * work groups have no ordering, so any step that reads a texel another invocation writes is a
 * data race, and `glMemoryBarrier` cannot fix it because it orders *commands*, not invocations
 * within one dispatch. The grid would have to fit in a single work group for in-place to be
 * safe, which it never does. So compute keeps the ping-pong; what it drops is the
 * rasterization, the framebuffer bind and the tile resolve around each step.
 *
 * ### Immutable storage, and why it is not optional
 *
 * Allocation is `glTexStorage2D`, never `glTexImage2D`. ES 3.1 raises `GL_INVALID_OPERATION`
 * for `glBindImageTexture` on a mutable-format texture, so a state texture allocated the
 * familiar way simply cannot be bound as an image — and the failure is an error flag nobody
 * reads plus a dispatch that writes nothing, not a crash.
 *
 * ### Why not `RenderTarget`
 *
 * `RenderTarget` is the owner of every offscreen **RGBA8 colour** target in the tree, and this
 * is not one. It allocates with `glTexImage2D` at a fixed `GL_RGBA8`, and both halves of that
 * are wrong here: the format is whatever the probe resolved for simulation state, and a
 * mutable-format texture cannot be bound as an image at all. A state pair is a different
 * resource with a different invariant, not an RGBA8 target with extra options — but any
 * re-authored ownership gate that walks the tree for `glGenFramebuffers` has to know this file
 * exists and why, exactly as it knows about the fluid solver's own buffers.
 *
 * ### The framebuffers
 *
 * Both paths get one per side. The fragment path draws into them every step, which is the cost
 * the compute tier exists to remove. The compute path binds one only to clear: ES has no
 * `glClearTexImage`, so the only way to zero a texture without writing a whole shader for it is
 * through a framebuffer. That happens on allocation and on an explicit reseed, never per frame,
 * so the per-step bind and tile resolve stay gone.
 */
internal class SimField(
    private val label: String,
    private val format: GlImageFormat,
    private val filterable: Boolean,
) {
    private class Side {
        var texture = 0
        var framebuffer = 0

        val ok: Boolean get() = texture != 0 && framebuffer != 0
    }

    private var front = Side()
    private var back = Side()

    var width = 0
        private set
    var height = 0
        private set

    /** The texture holding the current state: what a step reads and what the display samples. */
    val readTexture: Int get() = front.texture

    /** The texture a step writes into. Becomes [readTexture] after [swap]. */
    val writeTexture: Int get() = back.texture

    /** The framebuffer wrapping [writeTexture], for the fragment path's draw. */
    val writeFramebuffer: Int get() = back.framebuffer

    val ok: Boolean get() = front.ok && back.ok

    // Preallocated clear values. The clear is not per frame, but a two-element array allocated
    // inside a GL call is the habit this codebase deliberately does not have.
    private val zeroFloat = FloatArray(CHANNELS)
    private val zeroUint = IntArray(CHANNELS)

    /**
     * Allocates the pair at [w] x [h] if it is not already there, and zeroes it.
     * Returns false if either side failed, in which case nothing is bound and the caller skips.
     */
    fun ensure(
        w: Int,
        h: Int,
    ): Boolean {
        if (w <= 0 || h <= 0) return false
        if (ok && width == w && height == h) return true
        // Allocation binds framebuffers to check completeness, and this runs *inside* a step,
        // before the caller has saved anything. Capturing the draw binding here is what stops a
        // resize from silently leaving the scene drawing into framebuffer zero for a frame.
        val previous = IntArray(1)
        GLES30.glGetIntegerv(GLES30.GL_DRAW_FRAMEBUFFER_BINDING, previous, 0)
        release()
        front = allocate(w, h)
        back = allocate(w, h)
        val allocated = front.ok && back.ok
        if (allocated) {
            width = w
            height = h
            clear()
        } else {
            Log.w(TAG, "$label: could not allocate a ${w}x$h ${format.name} state pair")
            release()
        }
        GLES30.glBindFramebuffer(GLES30.GL_DRAW_FRAMEBUFFER, previous[0])
        return allocated
    }

    /** Exchanges the read and write sides. Called once per step, after the write. */
    fun swap() {
        val held = front
        front = back
        back = held
    }

    /**
     * Zeroes both sides.
     *
     * Through `glClearBufferuiv` / `glClearBufferfv`, never `glClearColor` + `glClear`. Two
     * reasons: an integer attachment has no float clear colour at all, so the familiar spelling
     * is not merely untidy but wrong for the packed encoding; and the typed clears leave the
     * context's clear colour untouched, which matters because everything else in the frame
     * shares it.
     */
    fun clear() {
        if (!ok) return
        val previous = IntArray(1)
        GLES30.glGetIntegerv(GLES30.GL_DRAW_FRAMEBUFFER_BINDING, previous, 0)
        clearSide(front)
        clearSide(back)
        GLES30.glBindFramebuffer(GLES30.GL_DRAW_FRAMEBUFFER, previous[0])
    }

    /** Deletes both sides. Requires a live context. */
    fun release() {
        releaseSide(front)
        releaseSide(back)
        width = 0
        height = 0
    }

    /** Drops both sides without calling GL, for a context that is already gone. */
    fun forget() {
        front = Side()
        back = Side()
        width = 0
        height = 0
    }

    private fun clearSide(side: Side) {
        GLES30.glBindFramebuffer(GLES30.GL_DRAW_FRAMEBUFFER, side.framebuffer)
        if (format.integerTexels) {
            GLES30.glClearBufferuiv(GLES30.GL_COLOR, 0, zeroUint, 0)
        } else {
            GLES30.glClearBufferfv(GLES30.GL_COLOR, 0, zeroFloat, 0)
        }
    }

    private fun allocate(
        w: Int,
        h: Int,
    ): Side {
        val side = Side()
        val ids = IntArray(1)
        GLES30.glGenTextures(1, ids, 0)
        side.texture = ids[0]
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, side.texture)
        // NEAREST unless the format was *proven* filterable. For an integer texture this is not
        // a preference: LINEAR on a usampler2D leaves the texture incomplete, and an incomplete
        // texture samples as zero rather than raising anything, so the whole simulation goes
        // black with no error to find.
        val filter = if (filterable) GLES30.GL_LINEAR else GLES30.GL_NEAREST
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MIN_FILTER, filter)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MAG_FILTER, filter)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_S, GLES30.GL_CLAMP_TO_EDGE)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_T, GLES30.GL_CLAMP_TO_EDGE)
        // One level. Mipmaps of simulation state are never sampled and allocating them would
        // add half again the bandwidth this layer is trying to spend carefully.
        GLES30.glTexStorage2D(GLES30.GL_TEXTURE_2D, 1, format.internalFormat, w, h)

        GLES30.glGenFramebuffers(1, ids, 0)
        side.framebuffer = ids[0]
        // GL_DRAW_FRAMEBUFFER throughout, never GL_FRAMEBUFFER: the latter sets the read
        // binding too, and nothing here has any business touching a caller's read target.
        GLES30.glBindFramebuffer(GLES30.GL_DRAW_FRAMEBUFFER, side.framebuffer)
        GLES30.glFramebufferTexture2D(
            GLES30.GL_DRAW_FRAMEBUFFER,
            GLES30.GL_COLOR_ATTACHMENT0,
            GLES30.GL_TEXTURE_2D,
            side.texture,
            0,
        )
        val complete =
            GLES30.glCheckFramebufferStatus(GLES30.GL_DRAW_FRAMEBUFFER) == GLES30.GL_FRAMEBUFFER_COMPLETE
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, 0)
        if (!complete) {
            Log.w(TAG, "$label: ${format.name} framebuffer incomplete at ${w}x$h")
            releaseSide(side)
        }
        return side
    }

    private fun releaseSide(side: Side) {
        if (side.framebuffer != 0) GLES30.glDeleteFramebuffers(1, intArrayOf(side.framebuffer), 0)
        if (side.texture != 0) GLES30.glDeleteTextures(1, intArrayOf(side.texture), 0)
        side.framebuffer = 0
        side.texture = 0
    }

    private companion object {
        const val TAG = "SimField"
        const val CHANNELS = 4
    }
}
