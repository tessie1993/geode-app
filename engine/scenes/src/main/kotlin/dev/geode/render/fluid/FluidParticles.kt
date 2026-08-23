package dev.geode.render.fluid

import android.content.Context
import android.opengl.GLES30
import dev.geode.engine.scenes.R
import dev.geode.render.scene.GlUtil

internal class FluidParticles(
    private val context: Context,
) {
    var drag = 0.5f

    var life = 6f

    private var side = 0
    private var count = 0
    private var state: FluidBuffers.DoubleMrt? = null
    private var updateProgram = 0
    private var seedProgram = 0
    private var renderProgram = 0
    private val uniforms = HashMap<Int, GlUtil.UniformCache>()
    private val quad = GlUtil.FullscreenTriangle()
    private var pointsVao = 0
    private var pointsVbo = 0
    private var seeded = false

    private val spawnData = FloatArray(FluidChoreography.MAX_SPAWN * 4)
    private val catchData = FloatArray(FluidChoreography.MAX_CATCH * 4)
    private var spawnCount = 1
    private var catchCount = 0

    var available = false
        private set

    init {
        spawnData[2] = 1f
        spawnData[3] = 0.35f
    }

    fun create(
        particleCount: Int,
        formats: FluidBuffers.Formats,
    ) {
        release()
        side = FluidMath.stateSide(particleCount)
        count = side * side
        val stateFmt = formats.rgba32 ?: formats.rgba
        state = FluidBuffers.DoubleMrt(side, side, stateFmt, stateFmt).also { it.create() }
        if (state?.ok != true) {
            android.util.Log.w("FluidSim", "particle MRT state FBO failed - particle layer disabled")
            release()
            return
        }

        try {
            val vert = GlUtil.loadShader(context, R.raw.fluid_base_vert)
            seedProgram = GlUtil.buildProgram(vert, GlUtil.loadShader(context, R.raw.fluid_particle_seed_frag))
            updateProgram = GlUtil.buildProgram(vert, GlUtil.loadShader(context, R.raw.fluid_particle_update_frag))
            renderProgram =
                GlUtil.buildProgram(
                    GlUtil.loadShader(context, R.raw.fluid_particle_vert),
                    GlUtil.loadShader(context, R.raw.fluid_particle_frag),
                )
        } catch (e: GlUtil.ShaderCompileException) {
            android.util.Log.w("FluidSim", "particle shader rejected by driver: ${e.message}")
            release()
            return
        }
        uniforms[seedProgram] = GlUtil.UniformCache(seedProgram)
        uniforms[updateProgram] = GlUtil.UniformCache(updateProgram)
        uniforms[renderProgram] = GlUtil.UniformCache(renderProgram)

        quad.create()

        val ids = IntArray(1)
        val texels = FloatArray(count * 2)
        var k = 0
        for (y in 0 until side) {
            for (x in 0 until side) {
                texels[k++] = (x + 0.5f) / side
                texels[k++] = (y + 0.5f) / side
            }
        }
        GLES30.glGenVertexArrays(1, ids, 0)
        pointsVao = ids[0]
        GLES30.glGenBuffers(1, ids, 0)
        pointsVbo = ids[0]
        GLES30.glBindVertexArray(pointsVao)
        GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER, pointsVbo)
        GLES30.glBufferData(GLES30.GL_ARRAY_BUFFER, texels.size * 4, floatBuf(texels), GLES30.GL_STATIC_DRAW)
        GLES30.glEnableVertexAttribArray(0)
        GLES30.glVertexAttribPointer(0, 2, GLES30.GL_FLOAT, false, 0, 0)
        GLES30.glBindVertexArray(0)
        seeded = false
        available = true
    }

    fun setChoreography(
        spawns: FloatArray,
        spawnPoints: Int,
        catches: FloatArray,
        catchPoints: Int,
    ) {
        spawns.copyInto(spawnData, endIndex = spawnData.size.coerceAtMost(spawns.size))
        catches.copyInto(catchData, endIndex = catchData.size.coerceAtMost(catches.size))
        spawnCount = spawnPoints.coerceIn(1, FluidChoreography.MAX_SPAWN)
        catchCount = catchPoints.coerceIn(0, FluidChoreography.MAX_CATCH)
    }

    fun step(
        dt: Float,
        velocityTex: Int,
        aspect: Float,
        flowScale: Float,
        timeSeconds: Float = 0f,
    ) {
        val st = state ?: return
        GLES30.glDisable(GLES30.GL_BLEND)
        quad.bind()
        if (!seeded) {
            GLES30.glUseProgram(seedProgram)
            GLES30.glUniform1f(loc(seedProgram, "uAspect"), aspect)
            GLES30.glUniform4fv(loc(seedProgram, "uSpawns"), FluidChoreography.MAX_SPAWN, spawnData, 0)
            GLES30.glUniform1i(loc(seedProgram, "uSpawnCount"), spawnCount)
            GLES30.glUniform1f(loc(seedProgram, "uLife"), life.coerceIn(1f, 30f))
            blitDiscarding(st.write)
            st.swap()
            seeded = true
        }
        GLES30.glUseProgram(updateProgram)
        GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, st.read.texA)
        GLES30.glUniform1i(loc(updateProgram, "uState"), 0)
        GLES30.glActiveTexture(GLES30.GL_TEXTURE1)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, st.read.texB)
        GLES30.glUniform1i(loc(updateProgram, "uMeta"), 1)
        GLES30.glActiveTexture(GLES30.GL_TEXTURE2)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, velocityTex)
        GLES30.glUniform1i(loc(updateProgram, "uVelocityField"), 2)
        GLES30.glUniform1f(loc(updateProgram, "uAspect"), aspect)
        GLES30.glUniform1f(loc(updateProgram, "uDt"), dt)
        GLES30.glUniform1f(loc(updateProgram, "uDrag"), drag.coerceIn(0.02f, 1f))
        GLES30.glUniform1f(loc(updateProgram, "uFlowScale"), flowScale)
        GLES30.glUniform1f(loc(updateProgram, "uTime"), timeSeconds % 256f)
        GLES30.glUniform1f(loc(updateProgram, "uLife"), life.coerceIn(1f, 30f))
        GLES30.glUniform4fv(loc(updateProgram, "uSpawns"), FluidChoreography.MAX_SPAWN, spawnData, 0)
        GLES30.glUniform1i(loc(updateProgram, "uSpawnCount"), spawnCount)
        GLES30.glUniform4fv(loc(updateProgram, "uCatches"), FluidChoreography.MAX_CATCH, catchData, 0)
        GLES30.glUniform1i(loc(updateProgram, "uCatchCount"), catchCount)
        blitDiscarding(st.write)
        st.swap()
        quad.unbind()
    }

    fun draw(
        aspect: Float,
        pointScale: Float,
        hueBase: Float,
        hueSpan: Float,
        brightness: Float,
        shape: Float = 0f,
        glow: Float = 0.85f,
        timeSeconds: Float = 0f,
    ) {
        val st = state ?: return
        GLES30.glEnable(GLES30.GL_BLEND)
        GLES30.glBlendFunc(GLES30.GL_ONE, GLES30.GL_ONE)
        GLES30.glUseProgram(renderProgram)
        GLES30.glBindVertexArray(pointsVao)
        GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, st.read.texA)
        GLES30.glUniform1i(loc(renderProgram, "uState"), 0)
        GLES30.glActiveTexture(GLES30.GL_TEXTURE1)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, st.read.texB)
        GLES30.glUniform1i(loc(renderProgram, "uMeta"), 1)
        GLES30.glUniform1f(loc(renderProgram, "uAspect"), aspect)
        GLES30.glUniform1f(loc(renderProgram, "uPointScale"), pointScale)
        GLES30.glUniform1f(loc(renderProgram, "uHueBase"), hueBase)
        GLES30.glUniform1f(loc(renderProgram, "uHueSpan"), hueSpan)
        GLES30.glUniform1f(loc(renderProgram, "uBrightness"), brightness)
        GLES30.glUniform1f(loc(renderProgram, "uShape"), shape)
        GLES30.glUniform1f(loc(renderProgram, "uGlow"), glow)
        GLES30.glUniform1f(loc(renderProgram, "uTime"), timeSeconds)
        GLES30.glDrawArrays(GLES30.GL_POINTS, 0, count)
        GLES30.glBindVertexArray(0)
        GLES30.glDisable(GLES30.GL_BLEND)
    }

    fun invalidateSeed() {
        seeded = false
    }

    fun release() {
        state?.release()
        state = null
        intArrayOf(updateProgram, seedProgram, renderProgram).forEach { if (it != 0) GLES30.glDeleteProgram(it) }
        updateProgram = 0
        seedProgram = 0
        renderProgram = 0
        uniforms.clear()
        quad.release()
        if (pointsVbo != 0) GLES30.glDeleteBuffers(1, intArrayOf(pointsVbo), 0)
        if (pointsVao != 0) GLES30.glDeleteVertexArrays(1, intArrayOf(pointsVao), 0)
        pointsVao = 0
        pointsVbo = 0
        seeded = false
        available = false
    }

    /**
     * Both the seed and the update kernel write outPosVel and outMeta on every path - including
     * the respawn early-out - over the whole state texture with blending off, and both sample the
     * read side rather than the side being written. So the write side's previous generation is
     * dead before the draw and the driver can skip pulling two float textures into tile memory.
     */
    private fun blitDiscarding(target: FluidBuffers.DoubleMrt.Side) {
        GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, target.fbo)
        target.discardContents()
        GLES30.glViewport(0, 0, side, side)
        GLES30.glDrawArrays(GLES30.GL_TRIANGLES, 0, 3)
    }

    private fun loc(
        program: Int,
        name: String,
    ): Int = uniforms.getValue(program).loc(name)

    private fun floatBuf(data: FloatArray) =
        java.nio.ByteBuffer
            .allocateDirect(data.size * 4)
            .order(java.nio.ByteOrder.nativeOrder())
            .asFloatBuffer()
            .put(data)
            .apply { position(0) }
}
