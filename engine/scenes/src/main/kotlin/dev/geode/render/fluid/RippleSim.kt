package dev.geode.render.fluid

import android.content.Context
import android.opengl.GLES30
import dev.geode.engine.scenes.R
import dev.geode.render.scene.GlUtil

internal class RippleSim(
    private val context: Context,
) {
    private class Drop(
        val x: Float,
        val y: Float,
        val radius: Float,
        val amplitude: Float,
        val r: Float,
        val g: Float,
        val b: Float,
    )

    companion object {
        private const val MAX_PENDING = 64

        private const val DROPS_PER_PASS = 8

        private const val MAX_SUBSTEPS = 6

        private const val INK_CEILING = 6f
    }

    var simRes = 384
        private set

    var waveSpeed = 1.2f

    var damping = 0.985f

    var inkEnabled = false

    var inkFlow = 1f

    var inkDissipation = 0.35f

    var onShaderError: (String?) -> Unit = {}

    var available = false
        private set

    private var width = 1
    private var height = 1
    var aspect = 1f
        private set

    private var cellSize = 2f / 384f

    private lateinit var formats: FluidBuffers.Formats
    private var grid: FluidBuffers.DoubleFbo? = null
    private var ink: FluidBuffers.DoubleFbo? = null
    private val quad = GlUtil.FullscreenTriangle()
    private val programs = HashMap<Int, GlUtil.UniformCache>()
    private val pending = ArrayList<Drop>()
    private val dropVec = FloatArray(DROPS_PER_PASS * 4)
    private val dropColorVec = FloatArray(DROPS_PER_PASS * 4)
    private val drained = ArrayList<Drop>()

    val heightTex: Int get() = grid?.read?.tex ?: 0

    val inkTex: Int get() = ink?.read?.tex ?: 0

    val inkAvailable: Boolean get() = ink?.ok == true
    val texelW: Float get() = 1f / (grid?.width ?: 1)
    val texelH: Float get() = 1f / (grid?.height ?: 1)

    val texFormats: FluidBuffers.Formats get() = formats

    fun create() {
        release()
        formats = FluidBuffers.probeFormats()
        available = formats.ok
        if (!available) return
        quad.create()
        val baseVert = GlUtil.loadShader(context, R.raw.fluid_base_vert)
        val frags =
            if (inkEnabled) {
                intArrayOf(
                    R.raw.ripple_splat_frag,
                    R.raw.ripple_update_frag,
                    R.raw.water_ink_splat_frag,
                    R.raw.water_ink_advect_frag,
                )
            } else {
                intArrayOf(R.raw.ripple_splat_frag, R.raw.ripple_update_frag)
            }
        try {
            for (f in frags) {
                programs[f] = GlUtil.UniformCache(GlUtil.buildProgram(baseVert, GlUtil.loadShader(context, f)))
            }
        } catch (e: GlUtil.ShaderCompileException) {
            android.util.Log.w("RippleSim", "ripple shader rejected by driver: ${e.message}")
            onShaderError("Water unavailable on this GPU: ${e.message}")
            release()
        }
    }

    fun resize(
        w: Int,
        h: Int,
    ): Boolean {
        if (!available) return false
        if (w == width && h == height && grid != null) return false
        width = w
        height = h
        aspect = w.toFloat() / h.coerceAtLeast(1)
        allocGrid()
        return true
    }

    fun applyResolution(newSimRes: Int): Boolean {
        if (!available) return false
        if (newSimRes == simRes && grid != null) return false
        simRes = newSimRes
        if (width > 1) allocGrid()
        return true
    }

    private fun allocGrid() {
        val prevFbo = IntArray(1)
        val prevVp = IntArray(4)
        GLES30.glGetIntegerv(GLES30.GL_FRAMEBUFFER_BINDING, prevFbo, 0)
        GLES30.glGetIntegerv(GLES30.GL_VIEWPORT, prevVp, 0)
        grid?.release()
        ink?.release()
        ink = null
        val (gw, gh) = FluidBuffers.resolution(simRes, width, height)
        grid = FluidBuffers.DoubleFbo(gw, gh, formats.rg, linear = true).also { it.create() }
        if (grid?.ok != true) {
            android.util.Log.w("RippleSim", "ripple grid allocation failed (${gw}x$gh) - water disabled")
            onShaderError("Water grid could not be allocated on this GPU")
            GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, prevFbo[0])
            GLES30.glViewport(prevVp[0], prevVp[1], prevVp[2], prevVp[3])
            release()
            return
        }
        if (inkEnabled && programs.containsKey(R.raw.water_ink_splat_frag)) {
            ink = FluidBuffers.DoubleFbo(gw, gh, formats.rgba, linear = true).also { it.create() }
            if (ink?.ok != true) {
                android.util.Log.w("RippleSim", "ink grid allocation failed (${gw}x$gh) - liquid layer off")
                ink?.release()
                ink = null
            } else {
                clearInk()
            }
        }
        cellSize = 2f / gh
        GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, prevFbo[0])
        GLES30.glViewport(prevVp[0], prevVp[1], prevVp[2], prevVp[3])
    }

    private fun clearInk() {
        val fbo = ink ?: return
        for (side in listOf(fbo.read, fbo.write)) {
            GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, side.fbo)
            GLES30.glViewport(0, 0, side.width, side.height)
            GLES30.glClearColor(0f, 0f, 0f, 0f)
            GLES30.glClear(GLES30.GL_COLOR_BUFFER_BIT)
        }
        GLES30.glClearColor(0f, 0f, 0f, 1f)
    }

    @Synchronized
    fun queueDrop(
        x: Float,
        y: Float,
        radius: Float,
        amplitude: Float,
        r: Float = 0f,
        g: Float = 0f,
        b: Float = 0f,
    ) {
        if (!available) return
        if (pending.size >= MAX_PENDING) return
        pending.add(Drop(x, y, radius, amplitude, r, g, b))
    }

    fun queueStroke(
        x: Float,
        y: Float,
        dx: Float,
        dy: Float,
        dt: Float,
        radius: Float,
        strength: Float,
        r: Float = 0f,
        g: Float = 0f,
        b: Float = 0f,
    ) {
        for (d in RippleMath.strokeDrops(x, y, dx, dy, dt, radius, strength)) {
            queueDrop(d.x, d.y, d.radius, d.amplitude, r, g, b)
        }
    }

    fun step(dtRaw: Float) {
        if (!available) return
        val g =
            grid ?: run {
                synchronized(this) { pending.clear() }
                return
            }
        drained.clear()
        synchronized(this) {
            drained.addAll(pending)
            pending.clear()
        }
        val dt = dtRaw.coerceIn(0f, 1f / 30f)
        GLES30.glDisable(GLES30.GL_BLEND)
        quad.bind()

        val inkFbo = ink
        var i = 0
        while (i < drained.size) {
            val n = minOf(DROPS_PER_PASS, drained.size - i)
            for (j in 0 until n) {
                val d = drained[i + j]
                dropVec[j * 4] = d.x
                dropVec[j * 4 + 1] = d.y
                dropVec[j * 4 + 2] = d.radius
                dropVec[j * 4 + 3] = d.amplitude
                dropColorVec[j * 4] = d.r
                dropColorVec[j * 4 + 1] = d.g
                dropColorVec[j * 4 + 2] = d.b
                dropColorVec[j * 4 + 3] = maxOf(d.r, maxOf(d.g, d.b))
            }
            useProgram(R.raw.ripple_splat_frag, g.width, g.height)
            bindTex("uTarget", g.read.tex, 0, R.raw.ripple_splat_frag)
            GLES30.glUniform4fv(loc(R.raw.ripple_splat_frag, "uDrops"), DROPS_PER_PASS, dropVec, 0)
            GLES30.glUniform1i(loc(R.raw.ripple_splat_frag, "uDropCount"), n)
            blitDiscarding(g.write)
            g.swap()
            if (inkFbo != null) {
                useProgram(R.raw.water_ink_splat_frag, inkFbo.width, inkFbo.height)
                bindTex("uTarget", inkFbo.read.tex, 0, R.raw.water_ink_splat_frag)
                GLES30.glUniform4fv(loc(R.raw.water_ink_splat_frag, "uDrops"), DROPS_PER_PASS, dropVec, 0)
                GLES30.glUniform4fv(loc(R.raw.water_ink_splat_frag, "uDropColor"), DROPS_PER_PASS, dropColorVec, 0)
                GLES30.glUniform1i(loc(R.raw.water_ink_splat_frag, "uDropCount"), n)
                set1f(R.raw.water_ink_splat_frag, "uCeiling", INK_CEILING)
                blitDiscarding(inkFbo.write)
                inkFbo.swap()
            }
            i += n
        }
        drained.clear()

        if (dt > 0f) {
            val c = waveSpeed.coerceAtLeast(1e-4f)
            val cfl = RippleMath.cflClampedDt(c, dt, cellSize)
            val substeps =
                kotlin.math
                    .ceil((dt / cfl).toDouble())
                    .toInt()
                    .coerceIn(1, MAX_SUBSTEPS)
            val subDt = RippleMath.cflClampedDt(c, dt / substeps, cellSize)
            val clampedDamping = damping.coerceIn(0.9f, 0.999f)
            val subDamping =
                Math
                    .pow(clampedDamping.toDouble(), (subDt * 60f).toDouble())
                    .toFloat()
            val subHeightDecay = RippleMath.heightDecayPerSubstep(clampedDamping, subDt)
            val k = c * c * subDt / (cellSize * cellSize)
            useProgram(R.raw.ripple_update_frag, g.width, g.height)
            set1f(R.raw.ripple_update_frag, "uK", k)
            set1f(R.raw.ripple_update_frag, "uDt", subDt)
            set1f(R.raw.ripple_update_frag, "uDamping", subDamping)
            set1f(R.raw.ripple_update_frag, "uHeightDecay", subHeightDecay)
            repeat(substeps) {
                bindTex("uHeight", g.read.tex, 0, R.raw.ripple_update_frag)
                blitDiscarding(g.write)
                g.swap()
            }
        }

        if (inkFbo != null && dt > 0f) {
            useProgram(R.raw.water_ink_advect_frag, inkFbo.width, inkFbo.height)
            bindTex("uInk", inkFbo.read.tex, 0, R.raw.water_ink_advect_frag)
            bindTex("uHeight", g.read.tex, 1, R.raw.water_ink_advect_frag)
            set1f(R.raw.water_ink_advect_frag, "uDt", dt)
            set1f(R.raw.water_ink_advect_frag, "uFlow", inkFlow.coerceIn(0f, 8f))
            set1f(R.raw.water_ink_advect_frag, "uKeep", RippleMath.inkDissipation(inkDissipation, dt))
            blitDiscarding(inkFbo.write)
            inkFbo.swap()
            GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
        }
        quad.unbind()
    }

    fun release() {
        grid?.release()
        grid = null
        ink?.release()
        ink = null
        programs.values.forEach { GLES30.glDeleteProgram(it.program) }
        programs.clear()
        quad.release()
        synchronized(this) { pending.clear() }
        available = false
    }

    private fun useProgram(
        fragId: Int,
        gridW: Int,
        gridH: Int,
    ) {
        val p = programs.getValue(fragId)
        GLES30.glUseProgram(p.program)
        GLES30.glUniform2f(p.loc("uInvRes"), 1f / gridW, 1f / gridH)
        GLES30.glUniform1f(p.loc("uAspect"), aspect)
    }

    /**
     * Every pass in this solver - drop splat, wave substep, ink splat, ink advect - is a
     * fullscreen triangle over the whole grid with blending off and no discard, and each writes
     * the ping-pong write side while sampling the read side. The destination's previous contents
     * are therefore dead before the draw starts, and saying so keeps the driver from paging a
     * full grid into tile memory per pass; the substep loop alone runs it up to MAX_SUBSTEPS times.
     *
     * The name carries the hazard: a pass that blends onto, or re-reads, its own destination must
     * not go through here.
     */
    private fun blitDiscarding(target: FluidBuffers.Fbo) {
        GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, target.fbo)
        target.discardContents()
        GLES30.glViewport(0, 0, target.width, target.height)
        GLES30.glDrawArrays(GLES30.GL_TRIANGLES, 0, 3)
    }

    private fun loc(
        fragId: Int,
        name: String,
    ): Int = programs.getValue(fragId).loc(name)

    private fun bindTex(
        name: String,
        tex: Int,
        unit: Int,
        fragId: Int,
    ) {
        GLES30.glActiveTexture(GLES30.GL_TEXTURE0 + unit)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, tex)
        GLES30.glUniform1i(loc(fragId, name), unit)
    }

    private fun set1f(
        id: Int,
        n: String,
        v: Float,
    ) = GLES30.glUniform1f(loc(id, n), v)
}
