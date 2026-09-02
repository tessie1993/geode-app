package dev.geode.render.scene

import android.opengl.GLES30
import java.nio.ByteBuffer
import java.nio.ByteOrder

/** The GL helpers the app's own export passes still use; the scenes themselves are native. */
object GlUtil {
    class ShaderCompileException(
        message: String,
    ) : RuntimeException(message)

    class FullscreenTriangle {
        var vao = 0
            private set
        private var vbo = 0

        fun create() {
            val ids = IntArray(1)
            GLES30.glGenVertexArrays(1, ids, 0)
            vao = ids[0]
            GLES30.glGenBuffers(1, ids, 0)
            vbo = ids[0]
            val quad = floatArrayOf(-1f, -1f, 3f, -1f, -1f, 3f)
            val buf =
                ByteBuffer
                    .allocateDirect(quad.size * 4)
                    .order(ByteOrder.nativeOrder())
                    .asFloatBuffer()
                    .put(quad)
                    .apply { position(0) }
            GLES30.glBindVertexArray(vao)
            GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER, vbo)
            GLES30.glBufferData(GLES30.GL_ARRAY_BUFFER, quad.size * 4, buf, GLES30.GL_STATIC_DRAW)
            GLES30.glEnableVertexAttribArray(0)
            GLES30.glVertexAttribPointer(0, 2, GLES30.GL_FLOAT, false, 0, 0)
            GLES30.glBindVertexArray(0)
        }

        fun bind() {
            GLES30.glBindVertexArray(vao)
        }

        fun unbind() {
            GLES30.glBindVertexArray(0)
        }

        fun draw() {
            if (vao == 0) create()
            GLES30.glBindVertexArray(vao)
            GLES30.glDrawArrays(GLES30.GL_TRIANGLES, 0, 3)
            GLES30.glBindVertexArray(0)
        }

        fun release() {
            if (vbo != 0) GLES30.glDeleteBuffers(1, intArrayOf(vbo), 0)
            if (vao != 0) GLES30.glDeleteVertexArrays(1, intArrayOf(vao), 0)
            vbo = 0
            vao = 0
        }

        fun forget() {
            vao = 0
            vbo = 0
        }
    }

    fun buildProgram(
        vertexSrc: String,
        fragmentSrc: String,
    ): Int {
        val vs = compile(GLES30.GL_VERTEX_SHADER, vertexSrc)
        val fs =
            try {
                compile(GLES30.GL_FRAGMENT_SHADER, fragmentSrc)
            } catch (e: ShaderCompileException) {
                GLES30.glDeleteShader(vs)
                throw e
            }
        val prog = GLES30.glCreateProgram()
        GLES30.glAttachShader(prog, vs)
        GLES30.glAttachShader(prog, fs)
        GLES30.glLinkProgram(prog)
        val status = IntArray(1)
        GLES30.glGetProgramiv(prog, GLES30.GL_LINK_STATUS, status, 0)
        GLES30.glDeleteShader(vs)
        GLES30.glDeleteShader(fs)
        if (status[0] == 0) {
            val log = GLES30.glGetProgramInfoLog(prog)
            GLES30.glDeleteProgram(prog)
            throw ShaderCompileException("Link failed: $log")
        }
        return prog
    }

    fun compile(
        type: Int,
        src: String,
    ): Int {
        val shader = GLES30.glCreateShader(type)
        GLES30.glShaderSource(shader, src)
        GLES30.glCompileShader(shader)
        val status = IntArray(1)
        GLES30.glGetShaderiv(shader, GLES30.GL_COMPILE_STATUS, status, 0)
        if (status[0] == 0) {
            val log = GLES30.glGetShaderInfoLog(shader)
            GLES30.glDeleteShader(shader)
            throw ShaderCompileException(log)
        }
        return shader
    }
}
