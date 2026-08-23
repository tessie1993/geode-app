package dev.geode.render.fluid

import android.content.Context
import android.opengl.GLES30
import dev.geode.engine.scenes.R
import dev.geode.render.scene.GlUtil

internal class FluidSim(
    private val context: Context,
    private val velocityOnly: Boolean = false,
) {
    class Splat(
        val prevX: Float,
        val prevY: Float,
        val curX: Float,
        val curY: Float,
        val radius: Float,
        val velX: Float,
        val velY: Float,
        val r: Float,
        val g: Float,
        val b: Float,
    )

    var simRes = 128
    var dyeRes = 512
    var densityDissipation = 1.0f
    var velocityDissipation = 0.2f
    var pressureDamp = 0.8f
    var pressureIterations = 20
    var curlStrength = 30f

    var dyeCeiling = 0f

    var chromaticAging = 0f

    var audioBass = 0f
    var audioMid = 0f
    var audioTreble = 0f
    var audioEnergy = 0f
    var audioBeat = 0f
    var timeSeconds = 0f

    var onShaderError: (String?) -> Unit = {}

    private var width = 1
    private var height = 1
    var aspect = 1f
        private set

    private var cellSize = 2f / 128f
    private var rdx = 1f / cellSize
    private var halfRdx = 0.5f / cellSize
    private var alpha = -cellSize * cellSize

    private lateinit var formats: FluidBuffers.Formats
    private var velocity: FluidBuffers.DoubleFbo? = null
    private var dye: FluidBuffers.DoubleFbo? = null
    private var pressure: FluidBuffers.DoubleFbo? = null
    private var divergence: FluidBuffers.Fbo? = null
    private var curl: FluidBuffers.Fbo? = null

    private val quad = GlUtil.FullscreenTriangle()

    private var linearSampler = 0
    private var baseVertSrc = ""
    private val programs = HashMap<Int, GlUtil.UniformCache>()
    private val pending = ArrayList<Splat>()

    private var customForce: GlUtil.UniformCache? = null
    private var customDye: GlUtil.UniformCache? = null
    private var pendingForceSrc: String? = null
    private var pendingDyeSrc: String? = null
    private var injectionDirty = false

    var available = false
        private set

    val texFormats: FluidBuffers.Formats get() = formats

    val flowScale: Float get() = 2f * rdx / (velocity?.height ?: 128)

    fun create() {
        release()
        formats = FluidBuffers.probeFormats()
        available = formats.ok
        if (!available) return
        quad.create()
        val ids = IntArray(1)
        GLES30.glGenSamplers(1, ids, 0)
        linearSampler = ids[0]
        GLES30.glSamplerParameteri(linearSampler, GLES30.GL_TEXTURE_MIN_FILTER, GLES30.GL_LINEAR)
        GLES30.glSamplerParameteri(linearSampler, GLES30.GL_TEXTURE_MAG_FILTER, GLES30.GL_LINEAR)
        GLES30.glSamplerParameteri(linearSampler, GLES30.GL_TEXTURE_WRAP_S, GLES30.GL_CLAMP_TO_EDGE)
        GLES30.glSamplerParameteri(linearSampler, GLES30.GL_TEXTURE_WRAP_T, GLES30.GL_CLAMP_TO_EDGE)
        val frags =
            intArrayOf(
                R.raw.fluid_splat_frag,
                R.raw.fluid_advect_frag,
                R.raw.fluid_curl_frag,
                R.raw.fluid_vorticity_frag,
                R.raw.fluid_divergence_frag,
                R.raw.fluid_pressure_frag,
                R.raw.fluid_gradient_frag,
                R.raw.fluid_clear_frag,
                R.raw.fluid_copy_frag,
                R.raw.fluid_display_frag,
            )
        baseVertSrc = GlUtil.loadShader(context, R.raw.fluid_base_vert)
        try {
            for (f in frags) {
                programs[f] = GlUtil.UniformCache(GlUtil.buildProgram(baseVertSrc, GlUtil.loadShader(context, f)))
            }
        } catch (e: GlUtil.ShaderCompileException) {
            android.util.Log.w("FluidSim", "base shader rejected by driver: ${e.message}")
            onShaderError("Fluid unavailable on this GPU: ${e.message}")
            release()
            return
        }
        injectionDirty = pendingForceSrc != null || pendingDyeSrc != null
    }

    fun resize(
        w: Int,
        h: Int,
    ): Boolean {
        if (!available) return false
        if (w == width && h == height && velocity != null) return false
        width = w
        height = h
        aspect = w.toFloat() / h.coerceAtLeast(1)
        allocGrids(preserve = true)
        return true
    }

    fun applyResolution(
        newSimRes: Int,
        newDyeRes: Int,
    ): Boolean {
        if (!available) return false
        if (newSimRes == simRes && newDyeRes == dyeRes && velocity != null) return false
        simRes = newSimRes
        dyeRes = newDyeRes
        if (width > 1) allocGrids(preserve = true)
        return true
    }

    private fun allocGrids(preserve: Boolean) {
        val prevFbo = IntArray(1)
        val prevVp = IntArray(4)
        GLES30.glGetIntegerv(GLES30.GL_FRAMEBUFFER_BINDING, prevFbo, 0)
        GLES30.glGetIntegerv(GLES30.GL_VIEWPORT, prevVp, 0)
        val (sw, sh) = FluidBuffers.resolution(simRes, width, height)
        val (dw, dh) = FluidBuffers.resolution(dyeRes, width, height)
        val oldVelocity = velocity
        val oldDye = dye
        pressure?.release()
        pressure = null
        divergence?.release()
        curl?.release()
        velocity = FluidBuffers.DoubleFbo(sw, sh, formats.rg, linear = false).also { it.create() }
        if (preserve && oldVelocity != null) {
            velocity?.let { if (it.ok && oldVelocity.ok) copyInto(oldVelocity.read, it.read) }
        }
        oldVelocity?.release()
        if (!velocityOnly) {
            dye = FluidBuffers.DoubleFbo(dw, dh, formats.rgba, linear = true).also { it.create() }
            if (preserve && oldDye != null) {
                dye?.let { if (it.ok && oldDye.ok) copyInto(oldDye.read, it.read) }
            }
        }
        oldDye?.release()
        pressure = FluidBuffers.DoubleFbo(sw, sh, formats.r, linear = false).also { it.create() }
        divergence = FluidBuffers.Fbo(sw, sh, formats.r, linear = false).also { it.create() }
        curl = FluidBuffers.Fbo(sw, sh, formats.r, linear = false).also { it.create() }
        val allOk =
            velocity?.ok == true &&
                pressure?.ok == true &&
                divergence?.ok == true &&
                curl?.ok == true &&
                (velocityOnly || dye?.ok == true)
        if (!allOk) {
            android.util.Log.w("FluidSim", "grid allocation failed (${sw}x$sh / ${dw}x$dh) - fluid disabled")
            onShaderError("Fluid grids could not be allocated on this GPU")
            GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, prevFbo[0])
            GLES30.glViewport(prevVp[0], prevVp[1], prevVp[2], prevVp[3])
            release()
            return
        }
        cellSize = 2f / sh
        rdx = 1f / cellSize
        halfRdx = 0.5f / cellSize
        alpha = -cellSize * cellSize
        GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, prevFbo[0])
        GLES30.glViewport(prevVp[0], prevVp[1], prevVp[2], prevVp[3])
    }

    private fun copyInto(
        src: FluidBuffers.Fbo,
        dst: FluidBuffers.Fbo,
    ) {
        GLES30.glDisable(GLES30.GL_BLEND)
        quad.bind()
        useProgram(R.raw.fluid_copy_frag, dst.width, dst.height)
        bindTex("uTexture", src.tex, 0, R.raw.fluid_copy_frag)
        blitDiscarding(dst)
        quad.unbind()
    }

    @Synchronized
    fun setInjectionShaders(
        forceSrc: String?,
        dyeSrc: String?,
    ) {
        pendingForceSrc = forceSrc
        pendingDyeSrc = dyeSrc
        injectionDirty = true
    }

    private fun compileInjectionIfNeeded() {
        val (force, dyeS) =
            synchronized(this) {
                if (!injectionDirty) return
                injectionDirty = false
                pendingForceSrc to pendingDyeSrc
            }
        var firstError: String? = null
        customForce =
            compileCustom(force, customForce) { firstError = firstError ?: it }
        customDye =
            compileCustom(dyeS, customDye) { firstError = firstError ?: it }
        onShaderError(firstError)
    }

    private fun compileCustom(
        src: String?,
        current: GlUtil.UniformCache?,
        reportError: (String?) -> Unit,
    ): GlUtil.UniformCache? {
        if (src.isNullOrBlank()) {
            current?.let { GLES30.glDeleteProgram(it.program) }
            return null
        }
        val p = GlUtil.buildProgramReporting(baseVertSrc, src, reportError)
        if (p == 0) return current
        current?.let { GLES30.glDeleteProgram(it.program) }
        return GlUtil.UniformCache(p)
    }

    fun queueSplat(s: Splat) {
        if (!available) return
        pending.add(s)
    }

    val dyeTex: Int get() = dye?.read?.tex ?: 0
    val velocityTex: Int get() = velocity?.read?.tex ?: 0
    val velocityGridHeight: Int get() = velocity?.height ?: 128

    fun step(dtRaw: Float) {
        if (!available) return
        val vel =
            velocity ?: run {
                pending.clear()
                return
            }
        val press =
            pressure ?: run {
                pending.clear()
                return
            }
        val div =
            divergence ?: run {
                pending.clear()
                return
            }
        val crl =
            curl ?: run {
                pending.clear()
                return
            }
        val dt = dtRaw.coerceIn(0f, 1f / 30f)
        compileInjectionIfNeeded()
        GLES30.glDisable(GLES30.GL_BLEND)
        quad.bind()
        val velInvW = 1f / vel.width
        val velInvH = 1f / vel.height

        useProgram(R.raw.fluid_advect_frag, vel.width, vel.height)
        bindTex("uVelocity", vel.read.tex, 0, R.raw.fluid_advect_frag)
        bindTex("uSource", vel.read.tex, 0, R.raw.fluid_advect_frag)
        set2f(R.raw.fluid_advect_frag, "uSrcInvRes", velInvW, velInvH)
        set2f(R.raw.fluid_advect_frag, "uVelInvRes", velInvW, velInvH)
        set1f(R.raw.fluid_advect_frag, "uDt", dt)
        set1f(R.raw.fluid_advect_frag, "uRdx", rdx)
        val vd = 1f + velocityDissipation * dt
        set3f(R.raw.fluid_advect_frag, "uDecay", vd, vd, vd)
        blitDiscarding(vel.write)
        vel.swap()

        runInjection(vel, mode = 0, custom = customForce, dt = dt)

        useProgram(R.raw.fluid_curl_frag, crl.width, crl.height)
        bindTex("uVelocity", vel.read.tex, 0, R.raw.fluid_curl_frag)
        set1f(R.raw.fluid_curl_frag, "uHalfRdx", halfRdx)
        blitDiscarding(crl)
        useProgram(R.raw.fluid_vorticity_frag, vel.width, vel.height)
        bindTex("uVelocity", vel.read.tex, 0, R.raw.fluid_vorticity_frag)
        bindTex("uCurl", crl.tex, 1, R.raw.fluid_vorticity_frag)
        set1f(R.raw.fluid_vorticity_frag, "uCurlStrength", curlStrength)
        set1f(R.raw.fluid_vorticity_frag, "uDx", cellSize)
        set1f(R.raw.fluid_vorticity_frag, "uDt", dt)
        blitDiscarding(vel.write)
        vel.swap()

        useProgram(R.raw.fluid_divergence_frag, div.width, div.height)
        bindTex("uVelocity", vel.read.tex, 0, R.raw.fluid_divergence_frag)
        set1f(R.raw.fluid_divergence_frag, "uHalfRdx", halfRdx)
        set2f(R.raw.fluid_divergence_frag, "uInvRes", 1f / div.width, 1f / div.height)
        blitDiscarding(div)
        useProgram(R.raw.fluid_clear_frag, press.width, press.height)
        bindTex("uTexture", press.read.tex, 0, R.raw.fluid_clear_frag)
        set1f(R.raw.fluid_clear_frag, "uValue", pressureDamp)
        blitDiscarding(press.write)
        press.swap()
        useProgram(R.raw.fluid_pressure_frag, press.width, press.height)
        bindTex("uDivergence", div.tex, 1, R.raw.fluid_pressure_frag)
        set1f(R.raw.fluid_pressure_frag, "uAlpha", alpha)
        set2f(R.raw.fluid_pressure_frag, "uInvRes", 1f / press.width, 1f / press.height)
        repeat(pressureIterations) {
            bindTex("uPressure", press.read.tex, 0, R.raw.fluid_pressure_frag)
            blitDiscarding(press.write)
            press.swap()
        }
        useProgram(R.raw.fluid_gradient_frag, vel.width, vel.height)
        bindTex("uPressure", press.read.tex, 0, R.raw.fluid_gradient_frag)
        bindTex("uVelocity", vel.read.tex, 1, R.raw.fluid_gradient_frag)
        set1f(R.raw.fluid_gradient_frag, "uHalfRdx", halfRdx)
        set2f(R.raw.fluid_gradient_frag, "uInvRes", 1f / vel.width, 1f / vel.height)
        blitDiscarding(vel.write)
        vel.swap()

        val dyeB = dye
        if (!velocityOnly && dyeB != null) {
            runInjection(dyeB, mode = 1, custom = customDye, dt = dt)
            useProgram(R.raw.fluid_advect_frag, dyeB.width, dyeB.height)
            bindTex("uVelocity", vel.read.tex, 0, R.raw.fluid_advect_frag)
            GLES30.glBindSampler(0, linearSampler)
            bindTex("uSource", dyeB.read.tex, 1, R.raw.fluid_advect_frag)
            set2f(R.raw.fluid_advect_frag, "uSrcInvRes", 1f / dyeB.width, 1f / dyeB.height)
            set2f(R.raw.fluid_advect_frag, "uVelInvRes", velInvW, velInvH)
            set1f(R.raw.fluid_advect_frag, "uDt", dt)
            set1f(R.raw.fluid_advect_frag, "uRdx", rdx)
            val a = chromaticAging.coerceIn(0f, 1f)
            val ddR = 1f + densityDissipation * (1f - 0.20f * a) * dt
            val ddG = 1f + densityDissipation * (1f + 0.35f * a) * dt
            val ddB = 1f + densityDissipation * (1f - 0.05f * a) * dt
            set3f(R.raw.fluid_advect_frag, "uDecay", ddR, ddG, ddB)
            blitDiscarding(dyeB.write)
            dyeB.swap()
            GLES30.glBindSampler(0, 0)
        }
        pending.clear()
        quad.unbind()
    }

    private fun runInjection(
        target: FluidBuffers.DoubleFbo,
        mode: Int,
        custom: GlUtil.UniformCache?,
        dt: Float,
    ) {
        for (s in pending) {
            val cache = custom ?: programs.getValue(R.raw.fluid_splat_frag)
            GLES30.glUseProgram(cache.program)
            GLES30.glUniform2f(cache.loc("uInvRes"), 1f / target.width, 1f / target.height)
            GLES30.glUniform1f(cache.loc("uAspect"), aspect)
            GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
            GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, target.read.tex)
            GLES30.glUniform1i(cache.loc("uTarget"), 0)
            GLES30.glUniform2f(cache.loc("uPrev"), s.prevX, s.prevY)
            GLES30.glUniform2f(cache.loc("uCur"), s.curX, s.curY)
            GLES30.glUniform1f(cache.loc("uRadius"), s.radius)
            if (mode == 0) {
                GLES30.glUniform3f(cache.loc("uValue"), s.velX, s.velY, 0f)
            } else {
                GLES30.glUniform3f(cache.loc("uValue"), s.r, s.g, s.b)
            }
            GLES30.glUniform1i(cache.loc("uMode"), mode)
            GLES30.glUniform1f(cache.loc("uCeiling"), dyeCeiling)
            if (custom != null) {
                GLES30.glUniform1f(cache.loc("uDt"), dt)
                GLES30.glUniform1f(cache.loc("uDx"), cellSize)
                GLES30.glUniform1f(cache.loc("uTime"), timeSeconds)
                GLES30.glUniform1f(cache.loc("uBass"), audioBass)
                GLES30.glUniform1f(cache.loc("uMid"), audioMid)
                GLES30.glUniform1f(cache.loc("uTreble"), audioTreble)
                GLES30.glUniform1f(cache.loc("uEnergy"), audioEnergy)
                GLES30.glUniform1f(cache.loc("uBeat"), audioBeat)
            }
            blit(target.write)
            target.swap()
        }
    }

    fun drawDisplay() {
        if (!available) return
        val d = dye ?: return
        quad.bind()
        val prog = programs.getValue(R.raw.fluid_display_frag)
        GLES30.glUseProgram(prog.program)
        GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, d.read.tex)
        GLES30.glUniform1i(loc(R.raw.fluid_display_frag, "uDye"), 0)
        GLES30.glUniform2f(loc(R.raw.fluid_display_frag, "uInvRes"), 1f / d.width, 1f / d.height)
        GLES30.glUniform2f(loc(R.raw.fluid_display_frag, "uTexelSize"), 1f / d.width, 1f / d.height)
        GLES30.glDrawArrays(GLES30.GL_TRIANGLES, 0, 3)
        quad.unbind()
    }

    fun probeDyeMax(): String {
        val d = dye ?: return "no dye buffer"
        return runCatching {
            val prev = IntArray(1)
            GLES30.glGetIntegerv(GLES30.GL_FRAMEBUFFER_BINDING, prev, 0)
            GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, d.read.fbo)
            val fmt = IntArray(1)
            val type = IntArray(1)
            GLES30.glGetIntegerv(GLES30.GL_IMPLEMENTATION_COLOR_READ_FORMAT, fmt, 0)
            GLES30.glGetIntegerv(GLES30.GL_IMPLEMENTATION_COLOR_READ_TYPE, type, 0)
            val n = 8
            val out: String
            if (type[0] == GLES30.GL_FLOAT) {
                val buf =
                    java.nio.ByteBuffer
                        .allocateDirect(n * n * 4 * 4)
                        .order(java.nio.ByteOrder.nativeOrder())
                GLES30.glReadPixels(d.width / 2 - n / 2, d.height / 2 - n / 2, n, n, GLES30.GL_RGBA, GLES30.GL_FLOAT, buf)
                val fb = buf.asFloatBuffer()
                var mx = 0f
                while (fb.hasRemaining()) mx = maxOf(mx, fb.get())
                out = "max=%.4f (float read, fmt=0x%x)".format(mx, fmt[0])
            } else {
                val buf =
                    java.nio.ByteBuffer
                        .allocateDirect(n * n * 4)
                        .order(java.nio.ByteOrder.nativeOrder())
                GLES30.glReadPixels(d.width / 2 - n / 2, d.height / 2 - n / 2, n, n, GLES30.GL_RGBA, GLES30.GL_UNSIGNED_BYTE, buf)
                var mx = 0
                while (buf.hasRemaining()) mx = maxOf(mx, buf.get().toInt() and 0xFF)
                out = "max=$mx/255 (byte read, type=0x${Integer.toHexString(type[0])})"
            }
            GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, prev[0])
            out
        }.getOrElse { "probe failed: ${it.message}" }
    }

    fun release() {
        velocity?.release()
        dye?.release()
        pressure?.release()
        divergence?.release()
        curl?.release()
        velocity = null
        dye = null
        pressure = null
        divergence = null
        curl = null
        programs.values.forEach { GLES30.glDeleteProgram(it.program) }
        programs.clear()
        customForce?.let { GLES30.glDeleteProgram(it.program) }
        customDye?.let { GLES30.glDeleteProgram(it.program) }
        customForce = null
        customDye = null
        quad.release()
        if (linearSampler != 0) GLES30.glDeleteSamplers(1, intArrayOf(linearSampler), 0)
        linearSampler = 0
        pending.clear()
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
     * The solver's default draw. Every engine pass here is a fullscreen triangle over the whole
     * grid with blending off and no discard in the shader, so the destination is rewritten texel
     * for texel and last frame's copy of it is dead on arrival - telling the driver that up front
     * saves it loading a full grid into tile memory per pass, and this solver runs nine of them
     * (plus the Jacobi iterations) every frame.
     *
     * Aliasing safety: [target] is either a single-use scratch grid (curl, divergence) or the
     * ping-pong write side - never the read side the pass samples - so a discard here can never
     * eat simulation state that is still needed.
     */
    private fun blitDiscarding(target: FluidBuffers.Fbo) {
        GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, target.fbo)
        target.discardContents()
        GLES30.glViewport(0, 0, target.width, target.height)
        GLES30.glDrawArrays(GLES30.GL_TRIANGLES, 0, 3)
    }

    /**
     * The same draw without the tile hint, for passes whose fragment shader is not ours: the
     * injection programs come from [setInjectionShaders] and may discard or write partial
     * coverage, and an undefined texel fed back into a ping-pong grid does not wash out - it
     * advects.
     */
    private fun blit(target: FluidBuffers.Fbo) {
        GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, target.fbo)
        GLES30.glViewport(0, 0, target.width, target.height)
        GLES30.glDrawArrays(GLES30.GL_TRIANGLES, 0, 3)
    }

    private fun blit(target: FluidBuffers.DoubleFbo) = blit(target.write)

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

    private fun set2f(
        id: Int,
        n: String,
        a: Float,
        b: Float,
    ) = GLES30.glUniform2f(loc(id, n), a, b)

    private fun set3f(
        id: Int,
        n: String,
        a: Float,
        b: Float,
        c: Float,
    ) = GLES30.glUniform3f(loc(id, n), a, b, c)
}
