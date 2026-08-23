package dev.geode.engine.gl

import android.opengl.GLES30
import android.opengl.GLES31
import android.util.Log

/**
 * A linked compute program, and the compile/link discipline that gets one.
 *
 * ### Why the reporting is louder than the graphics path's
 *
 * `GlUtil.buildProgramReporting` hands a failure to the scene, which shows "unavailable on this
 * GPU" and the user sees a message. A compute step that fails to compile has **no on-screen
 * symptom at all** — the layer falls back to the fragment path and the picture is correct. That
 * is the right behaviour and it is also how a compute shader stays broken for a year: nobody
 * finds out. So a failure here logs the driver's message *and* the numbered source, once, at
 * WARN. The cost is one log line per process on a device that is going to run the fragment
 * path anyway; the alternative is a tier that quietly never engages.
 *
 * ### Ownership
 *
 * [release] deletes the GL object and needs a live context. [forget] drops the handle without
 * calling GL, for the context-loss path where the object is already gone and deleting it would
 * be a call into a dead context. They are not interchangeable — the same distinction
 * `RenderTarget` and the scenes make.
 */
class ComputeProgram private constructor(
    /** The linked program name. Zero is not representable: construction fails instead. */
    val program: Int,
    /** The local size this program was compiled with. Read back off the driver, not assumed. */
    val localSize: WorkGroupSize,
) {
    private var released = false

    /** Makes this the current program. */
    fun use() {
        GLES30.glUseProgram(program)
    }

    /**
     * Uniform location by name, or -1. Not cached here on purpose: the caller that sets
     * uniforms every frame already owns a cache keyed by name (`GlUtil.UniformCache`), and a
     * second cache inside the program would be a second thing to invalidate on relink.
     */
    fun uniformLocation(name: String): Int = GLES30.glGetUniformLocation(program, name)

    /** Deletes the program. Requires a live context. */
    fun release() {
        if (released) return
        released = true
        GLES30.glDeleteProgram(program)
    }

    /** Drops the handle without calling GL, for a context that is already gone. */
    fun forget() {
        released = true
    }

    /** Raised for a compile or link failure. Carries the driver's own words. */
    class ComputeCompileException(
        message: String,
    ) : RuntimeException(message)

    companion object {
        private const val TAG = "ComputeProgram"

        /**
         * `glGetProgramiv(GL_COMPUTE_WORK_GROUP_SIZE)` writes three values, not one.
         */
        private const val WORK_GROUP_SIZE_COMPONENTS = 3

        /**
         * Compiles and links [source] as a compute shader.
         *
         * [expected] is the local size the caller substituted into the source. After linking,
         * the driver's own `GL_COMPUTE_WORK_GROUP_SIZE` is read back and compared: that is the
         * check which catches a substitution that silently did not happen — a source built with
         * a placeholder still in it links fine and then dispatches at 1x1x1, which is not a
         * crash, just a simulation running 64x slower than the profile assumed.
         *
         * Requires a current ES 3.1 context. Throws [ComputeCompileException] on any failure;
         * callers that want a value rather than an exception use [buildReporting].
         *
         * Three throws, one type, three distinct GL failures: the program object was never
         * created, the link failed, or the driver reported a work-group size other than the
         * one the source declared. Collapsing them would cost the caller the only
         * information that says which of the three happened.
         */
        @Suppress("ThrowsCount")
        fun build(
            label: String,
            source: String,
            expected: WorkGroupSize,
        ): ComputeProgram {
            val shader = compile(label, source)
            val program = GLES30.glCreateProgram()
            if (program == 0) {
                GLES30.glDeleteShader(shader)
                throw ComputeCompileException("$label: glCreateProgram returned 0")
            }
            GLES30.glAttachShader(program, shader)
            GLES30.glLinkProgram(program)
            val status = IntArray(1)
            GLES30.glGetProgramiv(program, GLES30.GL_LINK_STATUS, status, 0)
            // Detached and deleted either way: the shader object's storage is dead weight once
            // the program is linked, and leaking one per failed attempt is still a leak.
            GLES30.glDetachShader(program, shader)
            GLES30.glDeleteShader(shader)
            if (status[0] == 0) {
                val log = GLES30.glGetProgramInfoLog(program).orEmpty().ifBlank { NO_LOG }
                GLES30.glDeleteProgram(program)
                throw ComputeCompileException("$label: compute link failed: $log")
            }
            val linked = linkedLocalSize(program)
            if (linked != expected) {
                GLES30.glDeleteProgram(program)
                throw ComputeCompileException(
                    "$label: linked local size is $linked but the source was built for $expected; " +
                        "the layout qualifier substitution did not take",
                )
            }
            return ComputeProgram(program = program, localSize = linked)
        }

        /**
         * [build], with the failure as a value. Returns null and hands the message to
         * [onError]; the caller's move is the fragment path, never a crash and never silence.
         */
        fun buildReporting(
            label: String,
            source: String,
            expected: WorkGroupSize,
            onError: (String) -> Unit,
        ): ComputeProgram? =
            try {
                build(label, source, expected)
            } catch (e: ComputeCompileException) {
                Log.w(TAG, numbered(source))
                onError(e.message ?: "$label: compute build failed with no message")
                null
            }

        private const val NO_LOG = "(the driver returned no message)"

        private fun compile(
            label: String,
            source: String,
        ): Int {
            // GL_COMPUTE_SHADER is ES 3.1; on a 3.0 context this returns 0 and raises
            // GL_INVALID_ENUM. Callers reach here only through ComputeSupport.Available, so a
            // zero means the context lied about its version rather than that we forgot to ask.
            val shader = GLES31.glCreateShader(GLES31.GL_COMPUTE_SHADER)
            if (shader == 0) {
                throw ComputeCompileException(
                    "$label: glCreateShader(GL_COMPUTE_SHADER) returned 0; this context does not have compute",
                )
            }
            GLES30.glShaderSource(shader, source)
            GLES30.glCompileShader(shader)
            val status = IntArray(1)
            GLES30.glGetShaderiv(shader, GLES30.GL_COMPILE_STATUS, status, 0)
            if (status[0] == 0) {
                // Read the log before deleting: the log belongs to the shader object and is
                // gone the moment it is deleted, which is how a failure becomes "compile
                // failed:" with nothing after the colon.
                val log = GLES30.glGetShaderInfoLog(shader).orEmpty().ifBlank { NO_LOG }
                GLES30.glDeleteShader(shader)
                throw ComputeCompileException("$label: compute compile failed: $log")
            }
            return shader
        }

        private fun linkedLocalSize(program: Int): WorkGroupSize {
            val out = IntArray(WORK_GROUP_SIZE_COMPONENTS)
            GLES30.glGetProgramiv(program, GLES31.GL_COMPUTE_WORK_GROUP_SIZE, out, 0)
            return WorkGroupSize(x = out[0], y = out[1], z = out[2])
        }

        /**
         * Line-numbered source. Driver messages say "0:57: error" and a 200-line generated
         * shader is unreadable without the numbers — and this source is *generated*, so it is
         * not sitting in a file anyone can open to line 57.
         */
        private fun numbered(source: String): String =
            source
                .lineSequence()
                .mapIndexed { index, line -> "${index + 1}: $line" }
                .joinToString(separator = "\n")
    }
}
