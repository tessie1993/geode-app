package dev.geode.render.scene

import android.content.Context
import android.opengl.GLES30
import dev.geode.analysis.AudioFeatures
import dev.geode.engine.scenes.R
import dev.geode.render.LiveSignal
import dev.geode.render.TouchField
import dev.geode.render.fluid.FluidHue
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.abs
import kotlin.math.max

internal class BeamScene(
    private val context: Context,
) : Scene,
    PcmSink,
    TouchReactive {
    override val id: String = SceneIds.BEAM

    private companion object {
        const val SAMPLES = 512

        const val BASE_SIGMA = 0.006f

        const val QUADRATURE = SAMPLES / 4

        const val BASE_GAIN = 0.8f
    }

    private var program = 0
    private var uniforms = GlUtil.UniformCache(0)
    private var programOk = false
    private var vao = 0
    private var waveTex = 0

    private var params = SceneParams.DEFAULT
    private var width = 1
    private var height = 1

    private val samples = FloatArray(SAMPLES)
    private val pcm = FloatArray(SAMPLES * 8)
    private var pcmCount = 0

    override fun acceptPcm(
        samples: FloatArray,
        count: Int,
    ) {
        val n = count.coerceAtMost(pcm.size)
        if (n <= 0) return
        System.arraycopy(samples, count - n, pcm, 0, n)
        pcmCount = n
    }

    private val upload = ByteBuffer.allocateDirect(SAMPLES * 4).order(ByteOrder.nativeOrder())

    private val uploadFloats = upload.asFloatBuffer()

    private val beamRgb = FloatArray(3)

    private var autoGain = 1f

    private var beatPulse = 0f

    private var touch: TouchField? = null

    var onShaderError: (String?) -> Unit = {}

    override fun init() {
        program = 0
        vao = 0
        waveTex = 0
        uniforms = GlUtil.UniformCache(0)
        programOk = false
        program =
            GlUtil.buildProgramReporting(
                GlUtil.loadShader(context, R.raw.beam_vert),
                GlUtil.loadShader(context, R.raw.beam_frag),
            ) { onShaderError("Beam unavailable on this GPU: $it") }
        if (program == 0) return
        programOk = true
        uniforms = GlUtil.UniformCache(program)
        val ids = IntArray(1)
        GLES30.glGenVertexArrays(1, ids, 0)
        vao = ids[0]
        GLES30.glGenTextures(1, ids, 0)
        waveTex = ids[0]
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, waveTex)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MIN_FILTER, GLES30.GL_NEAREST)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MAG_FILTER, GLES30.GL_NEAREST)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_S, GLES30.GL_CLAMP_TO_EDGE)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_T, GLES30.GL_CLAMP_TO_EDGE)
        GLES30.glTexImage2D(GLES30.GL_TEXTURE_2D, 0, GLES30.GL_R32F, SAMPLES, 1, 0, GLES30.GL_RED, GLES30.GL_FLOAT, null)
    }

    override fun setParams(params: SceneParams) {
        this.params = params
    }

    override fun setTouchField(field: TouchField) {
        touch = field
    }

    override fun resize(
        width: Int,
        height: Int,
    ) {
        this.width = max(width, 1)
        this.height = max(height, 1)
    }

    override fun update(
        features: AudioFeatures,
        dt: Float,
    ) {
        val wave = features.waveform
        if (pcmCount > 0) {
            PcmRow.fill(samples, pcm, pcmCount)
            pcmCount = 0
        } else if (wave.isEmpty()) {
            samples.fill(0f)
        } else {
            PcmRow.fill(samples, wave, wave.size)
        }
        var peak = 0f
        for (i in samples.indices) peak = max(peak, abs(samples[i]))
        val target = if (peak > 0.02f) (0.85f / peak).coerceIn(0.5f, 6f) else autoGain
        autoGain += (target - autoGain) * (if (target < autoGain) 0.06f else 0.02f)
        beatPulse = max(LiveSignal.hit(features), beatPulse - dt * 3f).coerceIn(0f, 1.5f)
    }

    override fun draw(timeSeconds: Float) {
        if (!programOk) return
        GlUtil.resetFrameState()
        val p = params

        uploadFloats.clear()
        uploadFloats.put(samples)
        upload.position(0)
        GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, waveTex)
        GLES30.glTexSubImage2D(GLES30.GL_TEXTURE_2D, 0, 0, 0, SAMPLES, 1, GLES30.GL_RED, GLES30.GL_FLOAT, upload)

        GLES30.glUseProgram(program)
        GLES30.glUniform1i(loc("uWave"), 0)
        GLES30.glUniform1i(loc("uCount"), SAMPLES - 1)
        GLES30.glUniform1f(loc("uMode"), if (p.beamXy) 1f else 0f)
        GLES30.glUniform1i(loc("uPhaseOffset"), QUADRATURE)
        GLES30.glUniform1f(loc("uAspect"), width.toFloat() / height.toFloat())
        GLES30.glUniform1f(loc("uSigma"), BASE_SIGMA * p.beamWidth.coerceIn(0.2f, 4f))
        GLES30.glUniform1f(loc("uGain"), BASE_GAIN * p.audioDrive.coerceIn(0f, 4f) * autoGain)
        GLES30.glUniform1f(loc("uTail"), p.beamTail.coerceIn(0f, 1f))
        GLES30.glUniform1f(
            loc("uIntensity"),
            p.beamIntensity.coerceIn(0f, 3f) * (1f + beatPulse * p.beatResponse.coerceIn(0f, 2f) * 0.4f),
        )
        FluidHue.rgb(FluidHue.base(p.paletteBase), 1f, beamRgb)
        GLES30.glUniform3f(loc("uColor"), beamRgb[0], beamRgb[1], beamRgb[2])
        SceneTouch.upload(uniforms, touch)

        GLES30.glEnable(GLES30.GL_BLEND)
        GLES30.glBlendFunc(GLES30.GL_ONE, GLES30.GL_ONE)
        GLES30.glBindVertexArray(vao)
        GLES30.glDrawArrays(GLES30.GL_TRIANGLES, 0, (SAMPLES - 1) * 6)
        GLES30.glBindVertexArray(0)
        GLES30.glDisable(GLES30.GL_BLEND)
    }

    private fun loc(name: String): Int = uniforms.loc(name)

    override fun release() {
        if (program != 0) GLES30.glDeleteProgram(program)
        if (vao != 0) GLES30.glDeleteVertexArrays(1, intArrayOf(vao), 0)
        if (waveTex != 0) GLES30.glDeleteTextures(1, intArrayOf(waveTex), 0)
        program = 0
        vao = 0
        waveTex = 0
        programOk = false
        uniforms = GlUtil.UniformCache(0)
    }
}
