package dev.geode.render.fluid

import android.opengl.GLES30
import kotlin.math.roundToInt

internal object FluidBuffers {
    data class TexFormat(
        val internal: Int,
        val format: Int,
        val type: Int,
    )

    data class Formats(
        val r: TexFormat,
        val rg: TexFormat,
        val rgba: TexFormat,
        val rgba32: TexFormat?,
        val ok: Boolean,
    )

    fun probeFormats(): Formats {
        val rgba = TexFormat(GLES30.GL_RGBA16F, GLES30.GL_RGBA, GLES30.GL_HALF_FLOAT)
        val rg = TexFormat(GLES30.GL_RG16F, GLES30.GL_RG, GLES30.GL_HALF_FLOAT)
        val r = TexFormat(GLES30.GL_R16F, GLES30.GL_RED, GLES30.GL_HALF_FLOAT)

        fun renderable(f: TexFormat): Boolean {
            val tex = IntArray(1)
            val fbo = IntArray(1)
            GLES30.glGenTextures(1, tex, 0)
            GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, tex[0])
            GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MIN_FILTER, GLES30.GL_NEAREST)
            GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MAG_FILTER, GLES30.GL_NEAREST)
            GLES30.glTexImage2D(GLES30.GL_TEXTURE_2D, 0, f.internal, 4, 4, 0, f.format, f.type, null)
            GLES30.glGenFramebuffers(1, fbo, 0)
            GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, fbo[0])
            GLES30.glFramebufferTexture2D(
                GLES30.GL_FRAMEBUFFER,
                GLES30.GL_COLOR_ATTACHMENT0,
                GLES30.GL_TEXTURE_2D,
                tex[0],
                0,
            )
            val ok = GLES30.glCheckFramebufferStatus(GLES30.GL_FRAMEBUFFER) == GLES30.GL_FRAMEBUFFER_COMPLETE
            GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, 0)
            GLES30.glDeleteFramebuffers(1, fbo, 0)
            GLES30.glDeleteTextures(1, tex, 0)
            return ok
        }
        val rgba32 = TexFormat(GLES30.GL_RGBA32F, GLES30.GL_RGBA, GLES30.GL_FLOAT)
        val rgba32Ok = renderable(rgba32)
        val rgbaOk = renderable(rgba)
        val rgOk = renderable(rg)
        val rOk = renderable(r)
        val res =
            Formats(
                r =
                    if (rOk) {
                        r
                    } else if (rgOk) {
                        rg
                    } else {
                        rgba
                    },
                rg = if (rgOk) rg else rgba,
                rgba = rgba,
                rgba32 = if (rgba32Ok) rgba32 else null,
                ok = rgbaOk,
            )
        android.util.Log.i(
            "FluidSim",
            "fluid formats: R16F=${if (rOk) "ok" else "fb"} RG16F=${if (rgOk) "ok" else "fb"} " +
                "RGBA16F=${if (rgbaOk) "ok" else "MISSING"} RGBA32F=${if (rgba32Ok) "ok" else "no"}",
        )
        return res
    }

    fun resolution(
        res: Int,
        width: Int,
        height: Int,
    ): Pair<Int, Int> {
        if (width <= 0 || height <= 0) return res to res
        val aspect = width.toFloat() / height
        return if (aspect >= 1f) {
            (res * aspect).roundToInt().coerceAtLeast(2) to res
        } else {
            res to (res / aspect).roundToInt().coerceAtLeast(2)
        }
    }

    internal class Fbo(
        val width: Int,
        val height: Int,
        private val fmt: TexFormat,
        private val linear: Boolean,
    ) {
        var fbo = 0
            private set
        var tex = 0
            private set

        val ok: Boolean get() = fbo != 0 && tex != 0

        fun create() {
            val ids = IntArray(1)
            GLES30.glGenTextures(1, ids, 0)
            tex = ids[0]
            GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, tex)
            val filter = if (linear) GLES30.GL_LINEAR else GLES30.GL_NEAREST
            GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MIN_FILTER, filter)
            GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MAG_FILTER, filter)
            GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_S, GLES30.GL_CLAMP_TO_EDGE)
            GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_T, GLES30.GL_CLAMP_TO_EDGE)
            GLES30.glTexImage2D(GLES30.GL_TEXTURE_2D, 0, fmt.internal, width, height, 0, fmt.format, fmt.type, null)
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
            if (GLES30.glCheckFramebufferStatus(GLES30.GL_FRAMEBUFFER) != GLES30.GL_FRAMEBUFFER_COMPLETE) {
                android.util.Log.w("FluidSim", "FBO incomplete (${width}x$height fmt=0x${Integer.toHexString(fmt.internal)})")
                GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, 0)
                release()
                return
            }
            GLES30.glClearColor(0f, 0f, 0f, 1f)
            GLES30.glClear(GLES30.GL_COLOR_BUFFER_BIT)
            GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, 0)
        }

        /**
         * See [dev.geode.render.RenderTarget.discardContents] for the tile-load argument and the
         * bind-first contract. Only legal ahead of a pass that rewrites every texel with blending
         * off - which is every solver pass here except the user-authored injection shaders.
         */
        fun discardContents() {
            if (fbo == 0) return
            dev.geode.render.RenderTarget.discardColorAttachments(GLES30.GL_FRAMEBUFFER, 1)
        }

        fun release() {
            if (tex != 0) GLES30.glDeleteTextures(1, intArrayOf(tex), 0)
            if (fbo != 0) GLES30.glDeleteFramebuffers(1, intArrayOf(fbo), 0)
            tex = 0
            fbo = 0
        }
    }

    internal class DoubleMrt(
        val width: Int,
        val height: Int,
        private val fmtA: TexFormat,
        private val fmtB: TexFormat,
    ) {
        class Side(
            private val width: Int,
            private val height: Int,
            private val fmtA: TexFormat,
            private val fmtB: TexFormat,
        ) {
            var fbo = 0
                private set
            var texA = 0
                private set
            var texB = 0
                private set
            val ok: Boolean get() = fbo != 0 && texA != 0 && texB != 0

            fun create() {
                texA = makeTex(width, height, fmtA)
                texB = makeTex(width, height, fmtB)
                val ids = IntArray(1)
                GLES30.glGenFramebuffers(1, ids, 0)
                fbo = ids[0]
                GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, fbo)
                GLES30.glFramebufferTexture2D(
                    GLES30.GL_FRAMEBUFFER,
                    GLES30.GL_COLOR_ATTACHMENT0,
                    GLES30.GL_TEXTURE_2D,
                    texA,
                    0,
                )
                GLES30.glFramebufferTexture2D(
                    GLES30.GL_FRAMEBUFFER,
                    GLES30.GL_COLOR_ATTACHMENT1,
                    GLES30.GL_TEXTURE_2D,
                    texB,
                    0,
                )
                GLES30.glDrawBuffers(2, intArrayOf(GLES30.GL_COLOR_ATTACHMENT0, GLES30.GL_COLOR_ATTACHMENT1), 0)
                if (GLES30.glCheckFramebufferStatus(GLES30.GL_FRAMEBUFFER) != GLES30.GL_FRAMEBUFFER_COMPLETE) {
                    android.util.Log.w("FluidSim", "MRT FBO incomplete (${width}x$height)")
                    GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, 0)
                    release()
                    return
                }
                GLES30.glClearColor(0f, 0f, 0f, 0f)
                GLES30.glClear(GLES30.GL_COLOR_BUFFER_BIT)
                GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, 0)
            }

            private fun makeTex(
                w: Int,
                h: Int,
                fmt: TexFormat,
            ): Int {
                val ids = IntArray(1)
                GLES30.glGenTextures(1, ids, 0)
                GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, ids[0])
                GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MIN_FILTER, GLES30.GL_NEAREST)
                GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MAG_FILTER, GLES30.GL_NEAREST)
                GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_S, GLES30.GL_CLAMP_TO_EDGE)
                GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_T, GLES30.GL_CLAMP_TO_EDGE)
                GLES30.glTexImage2D(GLES30.GL_TEXTURE_2D, 0, fmt.internal, w, h, 0, fmt.format, fmt.type, null)
                return ids[0]
            }

            /**
             * Both attachments, because the particle kernels write outPosVel and outMeta on every
             * path - a half-discarded MRT would leave the meta texture undefined and recycle
             * particles at random. See [dev.geode.render.RenderTarget.discardContents].
             */
            fun discardContents() {
                if (fbo == 0) return
                dev.geode.render.RenderTarget.discardColorAttachments(GLES30.GL_FRAMEBUFFER, 2)
            }

            fun release() {
                if (texA != 0) GLES30.glDeleteTextures(1, intArrayOf(texA), 0)
                if (texB != 0) GLES30.glDeleteTextures(1, intArrayOf(texB), 0)
                if (fbo != 0) GLES30.glDeleteFramebuffers(1, intArrayOf(fbo), 0)
                texA = 0
                texB = 0
                fbo = 0
            }
        }

        var read = Side(width, height, fmtA, fmtB)
            private set
        var write = Side(width, height, fmtA, fmtB)
            private set
        val ok: Boolean get() = read.ok && write.ok

        fun create() {
            read.create()
            write.create()
        }

        fun swap() {
            val t = read
            read = write
            write = t
        }

        fun release() {
            read.release()
            write.release()
        }
    }

    internal class DoubleFbo(
        width: Int,
        height: Int,
        private val fmt: TexFormat,
        private val linear: Boolean,
    ) {
        var read = Fbo(width, height, fmt, linear)
            private set
        var write = Fbo(width, height, fmt, linear)
            private set

        val width: Int get() = read.width
        val height: Int get() = read.height
        val ok: Boolean get() = read.ok && write.ok

        fun create() {
            read.create()
            write.create()
        }

        fun swap() {
            val t = read
            read = write
            write = t
        }

        fun release() {
            read.release()
            write.release()
        }
    }
}
