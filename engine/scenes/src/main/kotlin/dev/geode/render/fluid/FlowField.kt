package dev.geode.render.fluid

import android.content.Context
import android.opengl.GLES30
import dev.geode.analysis.AudioFeatures
import dev.geode.engine.scenes.R
import dev.geode.render.scene.GlUtil
import dev.geode.render.scene.SceneParams

internal class FlowField(
    context: Context,
) {
    companion object {
        private const val GRID_RES = 64

        const val CPU_GRID = 32

        private const val KICK_FORCE = 0.22f
    }

    class CpuGrid {
        val data = FloatArray(CPU_GRID * CPU_GRID * 4)
        var scale = 0f
        var aspect = 1f

        fun sample(
            u: Float,
            v: Float,
            out: FloatArray,
        ) {
            val x = (u.coerceIn(0f, 1f) * (CPU_GRID - 1)).coerceAtLeast(0f)
            val y = (v.coerceIn(0f, 1f) * (CPU_GRID - 1)).coerceAtLeast(0f)
            val x0 = x.toInt().coerceAtMost(CPU_GRID - 2)
            val y0 = y.toInt().coerceAtMost(CPU_GRID - 2)
            val fx = x - x0
            val fy = y - y0

            fun at(
                gx: Int,
                gy: Int,
                c: Int,
            ) = data[(gy * CPU_GRID + gx) * 4 + c]
            for (c in 0 until 2) {
                val a = at(x0, y0, c) * (1 - fx) + at(x0 + 1, y0, c) * fx
                val b = at(x0, y0 + 1, c) * (1 - fx) + at(x0 + 1, y0 + 1, c) * fx
                out[c] = (a * (1 - fy) + b * fy) * scale
            }
            if (aspect > 1e-3f) out[0] /= aspect
        }
    }

    private val sim = FluidSim(context, velocityOnly = true)
    private val emitters =
        FluidEmitters().apply {
            beatPattern = FluidEmitters.PATTERN_RING
            beatSplats = 2
            stirrers = 2
            sparkle = false
        }

    val cpuGrid = CpuGrid()
    private var readFbo = 0
    private var readTex = 0
    private var copyProgram = 0
    private val copyQuad = GlUtil.FullscreenTriangle()
    private val readBuf =
        java.nio.ByteBuffer
            .allocateDirect(CPU_GRID * CPU_GRID * 4 * 4)
            .order(java.nio.ByteOrder.nativeOrder())

    private val readFloats = readBuf.asFloatBuffer()

    private val prevFbo = IntArray(1)
    private val prevViewport = IntArray(4)

    private val splats = ArrayList<FluidSim.Splat>()
    private var canReadback = false

    val available: Boolean get() = sim.available
    val velocityTex: Int get() = sim.velocityTex

    val flowScale: Float get() = sim.flowScale
    val aspect: Float get() = sim.aspect

    fun create() {
        release()
        sim.simRes = GRID_RES
        sim.pressureIterations = 12
        sim.create()
        if (!sim.available) return
        val fmt = sim.texFormats.rgba32
        canReadback = fmt != null
        if (fmt != null) {
            val ids = IntArray(1)
            GLES30.glGenTextures(1, ids, 0)
            readTex = ids[0]
            GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, readTex)
            GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MIN_FILTER, GLES30.GL_NEAREST)
            GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MAG_FILTER, GLES30.GL_NEAREST)
            GLES30.glTexImage2D(
                GLES30.GL_TEXTURE_2D,
                0,
                fmt.internal,
                CPU_GRID,
                CPU_GRID,
                0,
                fmt.format,
                fmt.type,
                null,
            )
            GLES30.glGenFramebuffers(1, ids, 0)
            readFbo = ids[0]
            GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, readFbo)
            GLES30.glFramebufferTexture2D(
                GLES30.GL_FRAMEBUFFER,
                GLES30.GL_COLOR_ATTACHMENT0,
                GLES30.GL_TEXTURE_2D,
                readTex,
                0,
            )
            GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, 0)
            copyProgram =
                GlUtil.buildProgramReporting(
                    GlUtil.loadShader(contextRef, R.raw.fluid_base_vert),
                    GlUtil.loadShader(contextRef, R.raw.fluid_copy_frag),
                ) {
                    android.util.Log.w("FluidSim", "flowfield copy shader rejected: $it")
                    canReadback = false
                }
            if (copyProgram != 0) copyQuad.create()
        }
    }

    fun resize(
        w: Int,
        h: Int,
    ) {
        sim.resize(w, h)
    }

    fun queueKick(
        clipX: Float,
        clipY: Float,
        velX: Float,
        velY: Float,
        radius: Float,
    ) {
        if (!sim.available) return
        val a = sim.aspect
        val x = clipX * a
        val y = clipY
        sim.queueSplat(
            FluidSim.Splat(
                prevX = x,
                prevY = y,
                curX = x,
                curY = y,
                radius = radius.coerceIn(0.02f, 0.4f),
                velX = velX * a * KICK_FORCE,
                velY = velY * KICK_FORCE,
                r = 0f,
                g = 0f,
                b = 0f,
            ),
        )
    }

    fun step(
        features: AudioFeatures,
        dt: Float,
        p: SceneParams,
    ) {
        if (!sim.available) return
        sim.curlStrength = p.flowCurl.coerceIn(0f, 50f)
        emitters.forceScale = p.flowForce.coerceIn(0f, 3f)
        emitters.stirrerSpeed = p.speed.coerceIn(0.1f, 2f)
        val simDt = dt.coerceIn(0f, 1f / 30f)
        emitters.tick(features, simDt, sim.aspect, 0f, 1f, splats)
        for (i in splats.indices) sim.queueSplat(splats[i])
        sim.step(simDt)
    }

    fun readback(
        sourceTex: Int,
        sourceFlowScale: Float,
        sourceAspect: Float,
    ) {
        if (!canReadback || sourceTex == 0) return
        GLES30.glGetIntegerv(GLES30.GL_FRAMEBUFFER_BINDING, prevFbo, 0)
        GLES30.glGetIntegerv(GLES30.GL_VIEWPORT, prevViewport, 0)
        GLES30.glDisable(GLES30.GL_BLEND)
        GLES30.glUseProgram(copyProgram)
        GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, readFbo)
        GLES30.glViewport(0, 0, CPU_GRID, CPU_GRID)
        GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, sourceTex)
        GLES30.glUniform1i(GLES30.glGetUniformLocation(copyProgram, "uTexture"), 0)
        copyQuad.draw()
        readBuf.clear()
        GLES30.glReadPixels(0, 0, CPU_GRID, CPU_GRID, GLES30.GL_RGBA, GLES30.GL_FLOAT, readBuf)
        GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, prevFbo[0])
        GLES30.glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3])
        readFloats.clear()
        readFloats.get(cpuGrid.data)
        cpuGrid.scale = sourceFlowScale
        cpuGrid.aspect = sourceAspect
    }

    fun release() {
        sim.release()
        if (readTex != 0) GLES30.glDeleteTextures(1, intArrayOf(readTex), 0)
        if (readFbo != 0) GLES30.glDeleteFramebuffers(1, intArrayOf(readFbo), 0)
        if (copyProgram != 0) GLES30.glDeleteProgram(copyProgram)
        copyQuad.release()
        readTex = 0
        readFbo = 0
        copyProgram = 0
        canReadback = false
    }

    private val contextRef = context.applicationContext
}
