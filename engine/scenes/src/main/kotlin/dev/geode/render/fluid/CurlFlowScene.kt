package dev.geode.render.fluid

import android.content.Context
import android.opengl.GLES30
import dev.geode.analysis.AudioFeatures
import dev.geode.engine.scenes.R
import dev.geode.render.LiveSignal
import dev.geode.render.scene.GlUtil
import dev.geode.render.scene.ParticleLook
import dev.geode.render.scene.SceneIds

internal class CurlFlowScene(
    private val context: Context,
) : FluidSceneBase(WALL_WRAP_SECONDS) {
    override val id: String = SceneIds.CURLFLOW

    private companion object {
        const val NOISE_WRAP_SECONDS = 628.31853f

        const val WALL_WRAP_SECONDS = 7100f
    }

    private val particles = FluidParticles(context)
    private lateinit var formats: FluidBuffers.Formats
    private var field: FluidBuffers.Fbo? = null
    private var fieldProgram = 0
    private var fieldUniforms = GlUtil.UniformCache(0)
    private val quad = GlUtil.FullscreenTriangle()

    private var noiseTime = 0f

    private var wallTime = 0f
    private var beatEnv = 0f

    private var beatDrive = 0f
    private var pcmKick = 0f
    private var aspect = 1f
    private var available = false

    override fun init() {
        release()
        formats = FluidBuffers.probeFormats()
        available = formats.ok
        if (!available) {
            onShaderError("Curl Flow unavailable: this GPU can't render half-float buffers")
            return
        }
        choreography.reset()
        quad.create()
        fieldProgram =
            GlUtil.buildProgramReporting(
                GlUtil.loadShader(context, R.raw.fluid_base_vert),
                GlUtil.loadShader(context, R.raw.curl_field_frag),
            ) { onShaderError("Curl Flow unavailable on this GPU: $it") }
        if (fieldProgram == 0) {
            release()
            return
        }
        fieldUniforms = GlUtil.UniformCache(fieldProgram)
        particles.create(49_152, formats)
        if (!particles.available) {
            onShaderError("Curl Flow unavailable: this GPU refused the particle state buffers")
            release()
        }
    }

    override fun resize(
        width: Int,
        height: Int,
    ) {
        if (!available) return
        aspect = width.toFloat() / height.coerceAtLeast(1)
        field?.release()
        val (fw, fh) = FluidBuffers.resolution(96, width, height)
        field =
            FluidBuffers.Fbo(fw, fh, formats.rg, linear = true)
                .also { it.create() }
                .takeIf { it.ok }
        if (field == null) onShaderError("Curl Flow unavailable: this GPU refused the flow-field buffer")
        particles.invalidateSeed()
    }

    override fun update(
        features: AudioFeatures,
        dt: Float,
    ) {
        pendingFeatures = features
        lastDt = dt.coerceIn(0f, 1f / 30f)
        pcmKick = tickPcm(dt).coerceIn(0f, 1f)
    }

    override fun idleFeatures(dt: Float): AudioFeatures = idleAudioFeatures(0f, 0f, 0f, 0f)

    override fun onApplyQualityTier(
        index: Int,
        userChanged: Boolean,
    ) = Unit

    private fun loc(name: String): Int = fieldUniforms.loc(name)

    override fun draw(timeSeconds: Float) {
        if (!available) return
        val fld = field ?: return
        val f = pendingFeatures
        saveFramebufferAndViewport()

        if (f != null) {
            wallTime = (wallTime + lastDt) % WALL_WRAP_SECONDS
            beatEnv = kotlin.math.max(LiveSignal.hit(f), beatEnv * kotlin.math.exp(-lastDt / 0.35f))
            beatDrive = CurlFlowMath.beatDrive(beatEnv, params.beatResponse)
            noiseTime = (noiseTime + lastDt * (0.15f + f.mid * 1.4f) * FluidChoreography.sceneSpeed(params.speed)) %
                NOISE_WRAP_SECONDS

            configureChoreography()
            choreography.tick(f, lastDt, aspect)

            GLES30.glDisable(GLES30.GL_BLEND)
            quad.bind()
            GLES30.glUseProgram(fieldProgram)
            GLES30.glUniform2f(loc("uInvRes"), 1f / fld.width, 1f / fld.height)
            GLES30.glUniform1f(loc("uAspect"), aspect)
            GLES30.glUniform1f(loc("uTime"), noiseTime)
            GLES30.glUniform1f(loc("uFreq"), 1.2f * (0.5f + params.turbulence.coerceIn(0.1f, 2f)))
            GLES30.glUniform1f(loc("uDetail"), (f.treble * 3f + pcmKick * 0.8f).coerceIn(0f, 1.5f))
            GLES30.glUniform1f(loc("uAmp"), CurlFlowMath.fieldAmp(params.audioDrive, beatDrive) * (1f + pcmKick * 0.35f))
            GLES30.glUniform2f(loc("uPeriod"), 0f, 0f)
            GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, fld.fbo)
            // The curl field is evaluated from noise, not integrated: this draw fills every texel
            // from uTime alone with blending off, and the only reader (particles.step, below) runs
            // after it. Nothing carries over between frames, so the old field is pure tile-load
            // cost. Note this whole block is skipped when there is no new audio frame - on those
            // frames the field is neither written nor read, so the discard cannot strand a reader.
            fld.discardContents()
            GLES30.glViewport(0, 0, fld.width, fld.height)
            GLES30.glDrawArrays(GLES30.GL_TRIANGLES, 0, 3)
            quad.unbind()

            applyChoreographyTo(particles)
            particles.step(lastDt, fld.tex, aspect, 1f, timeSeconds = wallTime)
            pendingFeatures = null
        }

        restoreFramebufferAndViewport()
        particles.draw(
            aspect,
            params.particleSize.coerceIn(0.4f, 4f) * viewportDpiScale(),
            params.paletteBase,
            FluidHue.span(params.hueRange, params.paletteRange),
            CurlFlowMath.particleBrightness(beatDrive),
            shape = params.particleShape.toFloat(),
            glow = ParticleLook.glow(params.bloom),
            timeSeconds = wallTime,
        )
    }

    override fun release() {
        particles.release()
        field?.release()
        field = null
        if (fieldProgram != 0) GLES30.glDeleteProgram(fieldProgram)
        fieldProgram = 0
        fieldUniforms = GlUtil.UniformCache(0)
        quad.release()
        available = false
    }
}
