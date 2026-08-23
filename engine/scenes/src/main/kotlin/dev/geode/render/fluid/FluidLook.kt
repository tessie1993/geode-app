package dev.geode.render.fluid

import android.content.Context
import android.opengl.GLES30
import dev.geode.engine.scenes.R
import dev.geode.render.scene.GlUtil

internal class FluidLook(
    private val context: Context,
) {
    companion object {
        private const val BLOOM_BASE_RES = 256
        private const val BLOOM_MAX_LEVELS = 8
        private const val SUNRAYS_RES = 196
    }

    var bloomIntensity = 0.8f
    var bloomThreshold = 0.6f
    var bloomKnee = 0.7f
    var sunraysWeight = 1.0f

    private lateinit var formats: FluidBuffers.Formats
    private var prefilterProgram = 0
    private var bloomBlurProgram = 0
    private var bloomFinalProgram = 0
    private var sunraysMaskProgram = 0
    private var sunraysProgram = 0
    private var blurProgram = 0

    private val displayPrograms = HashMap<Int, Int>()
    private val uniforms = HashMap<Int, GlUtil.UniformCache>()

    private var bloomMips = ArrayList<FluidBuffers.Fbo>()
    private var bloomResult: FluidBuffers.Fbo? = null
    private var sunraysMask: FluidBuffers.Fbo? = null
    private var sunrays: FluidBuffers.Fbo? = null
    private var sunraysTemp: FluidBuffers.Fbo? = null
    private var ditherTex = 0
    private val quad = GlUtil.FullscreenTriangle()
    private var targetW = 1
    private var targetH = 1

    var available = false
        private set

    fun create(fmts: FluidBuffers.Formats) {
        release()
        formats = fmts
        try {
            val vert = GlUtil.loadShader(context, R.raw.fluid_base_vert)
            prefilterProgram = GlUtil.buildProgram(vert, GlUtil.loadShader(context, R.raw.fluid_bloom_prefilter_frag))
            bloomBlurProgram = GlUtil.buildProgram(vert, GlUtil.loadShader(context, R.raw.fluid_bloom_blur_frag))
            bloomFinalProgram = GlUtil.buildProgram(vert, GlUtil.loadShader(context, R.raw.fluid_bloom_final_frag))
            sunraysMaskProgram = GlUtil.buildProgram(vert, GlUtil.loadShader(context, R.raw.fluid_sunrays_mask_frag))
            sunraysProgram = GlUtil.buildProgram(vert, GlUtil.loadShader(context, R.raw.fluid_sunrays_frag))
            blurProgram = GlUtil.buildProgram(vert, GlUtil.loadShader(context, R.raw.fluid_blur_frag))
            val displaySrc = GlUtil.loadShader(context, R.raw.fluid_display_frag)
            for (flags in 0 until 8) {
                displayPrograms[flags] = GlUtil.buildProgram(vert, withKeywords(displaySrc, flags))
            }
        } catch (e: GlUtil.ShaderCompileException) {
            android.util.Log.w("FluidSim", "look shader rejected by driver: ${e.message}")
            release()
            return
        }
        listOf(
            prefilterProgram,
            bloomBlurProgram,
            bloomFinalProgram,
            sunraysMaskProgram,
            sunraysProgram,
            blurProgram,
        ).forEach { uniforms[it] = GlUtil.UniformCache(it) }
        displayPrograms.values.forEach { uniforms[it] = GlUtil.UniformCache(it) }

        ditherTex = dev.geode.render.BlueNoise.createTexture(context)

        quad.create()
        available = true
    }

    fun resize(
        w: Int,
        h: Int,
    ) {
        if (!available) return
        if (w == targetW && h == targetH && bloomResult != null) return
        targetW = w
        targetH = h
        releaseTargets()
        val (bw, bh) = FluidBuffers.resolution(BLOOM_BASE_RES, w, h)
        bloomResult = FluidBuffers.Fbo(bw, bh, formats.rgba, linear = true).also { it.create() }
        bloomMips = ArrayList()
        var mw = bw
        var mh = bh
        for (i in 0 until BLOOM_MAX_LEVELS) {
            mw = mw shr 1
            mh = mh shr 1
            if (mw < 2 || mh < 2) break
            bloomMips.add(FluidBuffers.Fbo(mw, mh, formats.rgba, linear = true).also { it.create() })
        }
        val (sw, sh) = FluidBuffers.resolution(SUNRAYS_RES, w, h)
        sunraysMask = FluidBuffers.Fbo(sw, sh, formats.rgba, linear = true).also { it.create() }
        sunrays = FluidBuffers.Fbo(sw, sh, formats.r, linear = true).also { it.create() }
        sunraysTemp = FluidBuffers.Fbo(sw, sh, formats.r, linear = true).also { it.create() }
        val allOk =
            bloomResult?.ok == true &&
                bloomMips.all { it.ok } &&
                sunraysMask?.ok == true &&
                sunrays?.ok == true &&
                sunraysTemp?.ok == true
        if (!allOk) {
            android.util.Log.w("FluidSim", "look target allocation failed - bloom/sunrays disabled")
            releaseTargets()
        }
    }

    fun process(
        dyeTex: Int,
        bloomOn: Boolean,
        sunraysOn: Boolean,
    ) {
        if (!available) return
        GLES30.glDisable(GLES30.GL_BLEND)
        quad.bind()
        if (bloomOn) applyBloom(dyeTex)
        if (sunraysOn) applySunrays(dyeTex)
        quad.unbind()
    }

    private fun applyBloom(dyeTex: Int) {
        val result = bloomResult ?: return
        if (bloomMips.size < 2) return
        val knee = bloomThreshold * bloomKnee + 1e-4f
        var dst = bloomMips[0]
        use(prefilterProgram, 1f / dst.width, 1f / dst.height)
        bindTex(prefilterProgram, "uTexture", dyeTex, 0)
        GLES30.glUniform3f(
            loc(prefilterProgram, "uCurve"),
            bloomThreshold - knee,
            knee * 2f,
            0.25f / knee,
        )
        GLES30.glUniform1f(loc(prefilterProgram, "uThreshold"), bloomThreshold)
        blitDiscarding(dst)
        var last = dst
        // Down-chain: each mip is rebuilt from the level above it, so whatever this mip held from
        // last frame is never sampled again - discard it rather than pay to load it into tiles.
        for (i in 1 until bloomMips.size) {
            dst = bloomMips[i]
            use(bloomBlurProgram, 1f / last.width, 1f / last.height)
            bindTex(bloomBlurProgram, "uTexture", last.tex, 0)
            blitDiscarding(dst)
            last = dst
        }
        GLES30.glBlendFunc(GLES30.GL_ONE, GLES30.GL_ONE)
        GLES30.glEnable(GLES30.GL_BLEND)
        // Up-chain: additive. The destination mip still holds this frame's down-chain value and
        // the blend reads it back, so these MUST stay plain blits - a discard here would throw
        // away the very energy the chain is accumulating.
        for (i in bloomMips.size - 2 downTo 0) {
            dst = bloomMips[i]
            use(bloomBlurProgram, 1f / last.width, 1f / last.height)
            bindTex(bloomBlurProgram, "uTexture", last.tex, 0)
            blit(dst)
            last = dst
        }
        GLES30.glDisable(GLES30.GL_BLEND)
        use(bloomFinalProgram, 1f / last.width, 1f / last.height)
        bindTex(bloomFinalProgram, "uTexture", last.tex, 0)
        GLES30.glUniform1f(loc(bloomFinalProgram, "uIntensity"), bloomIntensity)
        blitDiscarding(result)
    }

    private fun applySunrays(dyeTex: Int) {
        val mask = sunraysMask ?: return
        val rays = sunrays ?: return
        val temp = sunraysTemp ?: return
        // Straight chain, blending off: dye -> mask -> rays -> temp -> rays. Each step's source is
        // the step before it, never the destination, so every destination is fully rewritten from
        // fresh input. The second write to rays is safe for the same reason - it reads temp.
        use(sunraysMaskProgram, 1f / mask.width, 1f / mask.height)
        bindTex(sunraysMaskProgram, "uTexture", dyeTex, 0)
        blitDiscarding(mask)
        use(sunraysProgram, 1f / rays.width, 1f / rays.height)
        bindTex(sunraysProgram, "uTexture", mask.tex, 0)
        GLES30.glUniform1f(loc(sunraysProgram, "uWeight"), sunraysWeight)
        blitDiscarding(rays)
        use(blurProgram, 1f / rays.width, 1f / rays.height)
        bindTex(blurProgram, "uTexture", rays.tex, 0)
        GLES30.glUniform2f(loc(blurProgram, "uDirection"), 1.33333f / rays.width, 0f)
        blitDiscarding(temp)
        bindTex(blurProgram, "uTexture", temp.tex, 0)
        GLES30.glUniform2f(loc(blurProgram, "uDirection"), 0f, 1.33333f / rays.height)
        blitDiscarding(rays)
    }

    fun drawDisplay(
        dyeTex: Int,
        shadingOn: Boolean,
        bloomOn: Boolean,
        sunraysOn: Boolean,
        viewportW: Int,
        viewportH: Int,
    ) {
        if (!available) return
        val bloom = bloomResult
        val rays = sunrays
        val flags =
            (if (shadingOn) 1 else 0) or
                (if (bloomOn && bloomMips.size >= 2 && bloom != null) 2 else 0) or
                (if (sunraysOn && rays != null) 4 else 0)
        val program = displayPrograms.getValue(flags)
        GLES30.glEnable(GLES30.GL_BLEND)
        GLES30.glBlendFunc(GLES30.GL_ONE, GLES30.GL_ONE_MINUS_SRC_ALPHA)
        use(program, 1f / viewportW, 1f / viewportH)
        bindTex(program, "uDye", dyeTex, 0)
        if (bloom != null && flags and 2 != 0) bindTex(program, "uBloom", bloom.tex, 1)
        if (rays != null && flags and 4 != 0) bindTex(program, "uSunrays", rays.tex, 2)
        bindTex(program, "uDither", ditherTex, 3)
        GLES30.glUniform2f(
            loc(program, "uDitherScale"),
            viewportW.toFloat() / dev.geode.render.BlueNoise.SIZE,
            viewportH.toFloat() / dev.geode.render.BlueNoise.SIZE,
        )
        GLES30.glUniform2f(loc(program, "uTexelSize"), 1f / viewportW, 1f / viewportH)
        quad.draw()
        GLES30.glDisable(GLES30.GL_BLEND)
    }

    fun release() {
        releaseTargets()
        (
            listOf(
                prefilterProgram,
                bloomBlurProgram,
                bloomFinalProgram,
                sunraysMaskProgram,
                sunraysProgram,
                blurProgram,
            ) + displayPrograms.values
        ).forEach { if (it != 0) GLES30.glDeleteProgram(it) }
        prefilterProgram = 0
        bloomBlurProgram = 0
        bloomFinalProgram = 0
        sunraysMaskProgram = 0
        sunraysProgram = 0
        blurProgram = 0
        displayPrograms.clear()
        uniforms.clear()
        if (ditherTex != 0) GLES30.glDeleteTextures(1, intArrayOf(ditherTex), 0)
        ditherTex = 0
        quad.release()
        targetW = 1
        targetH = 1
        available = false
    }

    private fun releaseTargets() {
        bloomMips.forEach { it.release() }
        bloomMips = ArrayList()
        bloomResult?.release()
        bloomResult = null
        sunraysMask?.release()
        sunraysMask = null
        sunrays?.release()
        sunrays = null
        sunraysTemp?.release()
        sunraysTemp = null
    }

    private fun withKeywords(
        src: String,
        flags: Int,
    ): String {
        val defines = StringBuilder()
        if (flags and 1 != 0) defines.append("#define SHADING\n")
        if (flags and 2 != 0) defines.append("#define BLOOM\n")
        if (flags and 4 != 0) defines.append("#define SUNRAYS\n")
        if (defines.isEmpty()) return src
        val vIdx = src.indexOf("#version")
        val nl = src.indexOf('\n', if (vIdx >= 0) vIdx else 0)
        return src.substring(0, nl + 1) + defines + src.substring(nl + 1)
    }

    private fun use(
        program: Int,
        invW: Float,
        invH: Float,
    ) {
        GLES30.glUseProgram(program)
        GLES30.glUniform2f(loc(program, "uInvRes"), invW, invH)
    }

    /**
     * For a pass that rewrites the whole of [target] with blending off. Telling the driver the old
     * contents are dead is worth more here than anywhere else in the engine: the bloom chain binds
     * the whole BLOOM_MAX_LEVELS mip pyramid down and back up every frame, and a tiler charges a
     * load and a store for each bind it thinks is incremental.
     */
    private fun blitDiscarding(target: FluidBuffers.Fbo) {
        GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, target.fbo)
        target.discardContents()
        GLES30.glViewport(0, 0, target.width, target.height)
        GLES30.glDrawArrays(GLES30.GL_TRIANGLES, 0, 3)
    }

    /** For a pass that blends onto what is already in [target] - the additive bloom up-chain. */
    private fun blit(target: FluidBuffers.Fbo) {
        GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, target.fbo)
        GLES30.glViewport(0, 0, target.width, target.height)
        GLES30.glDrawArrays(GLES30.GL_TRIANGLES, 0, 3)
    }

    private fun loc(
        program: Int,
        name: String,
    ): Int = uniforms.getValue(program).loc(name)

    private fun bindTex(
        program: Int,
        name: String,
        tex: Int,
        unit: Int,
    ) {
        GLES30.glActiveTexture(GLES30.GL_TEXTURE0 + unit)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, tex)
        GLES30.glUniform1i(loc(program, name), unit)
    }
}
