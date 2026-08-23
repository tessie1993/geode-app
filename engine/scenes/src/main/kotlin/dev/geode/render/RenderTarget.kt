package dev.geode.render

import android.opengl.GLES30

class RenderTarget(
    private val label: String,
) {
    var fbo: Int = 0
        private set
    var tex: Int = 0
        private set
    var width: Int = 0
        private set
    var height: Int = 0
        private set

    val ok: Boolean
        get() = fbo != 0 && tex != 0

    fun ensure(
        w: Int,
        h: Int,
    ): Boolean {
        if (degenerate(w, h) || alreadyAt(w, h)) return ok
        release()
        val ids = IntArray(1)
        GLES30.glGenTextures(1, ids, 0)
        tex = ids[0]
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, tex)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MIN_FILTER, GLES30.GL_LINEAR)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MAG_FILTER, GLES30.GL_LINEAR)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_S, GLES30.GL_CLAMP_TO_EDGE)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_T, GLES30.GL_CLAMP_TO_EDGE)
        GLES30.glTexImage2D(
            GLES30.GL_TEXTURE_2D,
            0,
            GLES30.GL_RGBA8,
            w,
            h,
            0,
            GLES30.GL_RGBA,
            GLES30.GL_UNSIGNED_BYTE,
            null,
        )
        GLES30.glGenFramebuffers(1, ids, 0)
        fbo = ids[0]
        GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, fbo)
        GLES30.glFramebufferTexture2D(
            GLES30.GL_FRAMEBUFFER,
            GLES30.GL_COLOR_ATTACHMENT0,
            GLES30.GL_TEXTURE_2D,
            tex,
            0,
        )
        val complete = GLES30.glCheckFramebufferStatus(GLES30.GL_FRAMEBUFFER) == GLES30.GL_FRAMEBUFFER_COMPLETE
        if (complete) {
            GLES30.glClearColor(0f, 0f, 0f, 1f)
            GLES30.glClear(GLES30.GL_COLOR_BUFFER_BIT)
            width = w
            height = h
        }
        GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, 0)
        if (!complete) release()
        return complete
    }

    private fun degenerate(
        w: Int,
        h: Int,
    ): Boolean = w <= 0 || h <= 0

    private fun alreadyAt(
        w: Int,
        h: Int,
    ): Boolean = ok && width == w && height == h

    fun release() {
        if (fbo != 0) GLES30.glDeleteFramebuffers(1, intArrayOf(fbo), 0)
        if (tex != 0) GLES30.glDeleteTextures(1, intArrayOf(tex), 0)
        forget()
    }

    fun forget() {
        fbo = 0
        tex = 0
        width = 0
        height = 0
    }

    /**
     * Tells the driver this target's current colour is dead, so the next pass may start with an
     * undefined tile instead of a loaded one.
     *
     * Mobile GPUs render in tiles: binding a target whose contents the driver believes are still
     * live costs a full-resolution DRAM read to seed tile memory before the first fragment lands,
     * and a store on the way out. A pass that overwrites every pixel never needed that read.
     *
     * Contract: bind [fbo] to [bindTarget] first, then call this, then issue the overwriting draw
     * or blit. Never call it ahead of a pass that blends onto, samples, or otherwise reads back
     * what is already in the target - the contents after an invalidate are undefined, and on a
     * tiler that shows up as garbage rather than as black.
     */
    fun discardContents(bindTarget: Int = GLES30.GL_FRAMEBUFFER) {
        // GL_COLOR_ATTACHMENT0 is only a legal attachment name for a real FBO; on the default
        // framebuffer it is GL_COLOR, and passing the wrong one raises GL_INVALID_ENUM that the
        // fluid scenes' first-frames glGetError diagnostic would then report as a driver problem.
        if (fbo == 0) return
        discardColorAttachments(bindTarget, 1)
    }

    override fun toString(): String = "RenderTarget($label ${width}x$height fbo=$fbo tex=$tex)"

    companion object {
        // Reused rather than built per call: discards run several times per frame on the GL
        // thread, and this is one of the hot paths CLAUDE.md exempts from immutable style.
        private val COLOR_ATTACHMENTS =
            intArrayOf(GLES30.GL_COLOR_ATTACHMENT0, GLES30.GL_COLOR_ATTACHMENT1)

        /**
         * [discardContents] for colour FBOs that are not [RenderTarget]s - the fluid solver keeps
         * its own float/MRT targets. [count] is how many colour attachments the FBO bound to
         * [bindTarget] actually has; naming an attachment the FBO does not own is a no-op per
         * spec, but keeping the count honest keeps the call site's coverage argument readable.
         */
        fun discardColorAttachments(
            bindTarget: Int,
            count: Int,
        ) {
            GLES30.glInvalidateFramebuffer(bindTarget, count, COLOR_ATTACHMENTS, 0)
        }
    }
}
