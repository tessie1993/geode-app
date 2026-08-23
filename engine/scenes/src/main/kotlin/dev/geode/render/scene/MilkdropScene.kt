package dev.geode.render.scene

import android.opengl.GLES30
import android.os.SystemClock
import dev.geode.analysis.AudioFeatures
import dev.geode.render.CompositeGrade
import dev.geode.render.LiveSignal
import java.io.File

class MilkdropScene(
    private val postVertexSrc: String,
    private val postFragmentSrc: String,
    private val sharedTextureDir: String?,
    private val onError: (String?) -> Unit = {},
    private val onPresetLoaded: (String) -> Unit = {},
) : Scene,
    PcmSink {
    companion object {
        private const val LOAD_DEBOUNCE_MS = 400L

        private const val DIAG_FRAMES = 90

        private const val DIAG_WARMUP = 20

        private val PROBE_BYTES = intArrayOf(0, 1, 2, 4, 5, 6)

        private const val PCM_CAPACITY = 8192

        private const val TWO_PI = (2.0 * Math.PI).toFloat()
    }

    override val id: String = SceneIds.MILKDROP

    private val pcmBuffer = FloatArray(PCM_CAPACITY)
    private var pcmCount = 0

    override fun acceptPcm(
        samples: FloatArray,
        count: Int,
    ) {
        val n = count.coerceAtMost(PCM_CAPACITY)
        if (n <= 0) return
        if (pcmCount + n > PCM_CAPACITY) pcmCount = 0
        System.arraycopy(samples, count - n, pcmBuffer, pcmCount, n)
        pcmCount += n
    }

    private var handle: Long = 0

    private var width = 0
    private var height = 0

    private var windowWidth = 0
    private var windowHeight = 0

    private var reportedCreateFailure = false
    private var frameTex = 0
    private var texWidth = 0
    private var texHeight = 0
    private var engineWidth = 0
    private var engineHeight = 0
    private var postProgram = 0
    private var postProgramOk = false

    private val postLocs = HashMap<String, Int>()
    private var postVao = 0
    private var rotationAngle = 0f
    private var zoomPhase = 0f
    private var cyclePhase = 0f
    private var beatPulse = 0f
    private var lastLoadMs = 0L
    private var sceneParams: SceneParams = SceneParams.DEFAULT
    private var diagFrames = 0
    private var diagDone = false

    private val prevFbo = IntArray(1)
    private val prevReadFbo = IntArray(1)

    private val diagPixels: java.nio.ByteBuffer =
        java.nio.ByteBuffer
            .allocateDirect(8)
            .order(java.nio.ByteOrder.nativeOrder())

    @Volatile
    private var pendingPresetPath: String? = null

    @Volatile
    private var lastPresetPath: String? = null

    override fun setParams(params: SceneParams) {
        sceneParams = params
    }

    fun queuePreset(path: String) {
        pendingPresetPath = path
    }

    fun reloadCurrent() {
        lastPresetPath?.let {
            pendingPresetPath = it
            lastLoadMs = 0L
        }
    }

    fun setWindowSize(
        width: Int,
        height: Int,
    ) {
        windowWidth = width
        windowHeight = height
    }

    override fun init() {
        release()
        reportedCreateFailure = false
        diagFrames = 0
        diagDone = false
        try {
            postProgram = GlUtil.buildProgram(postVertexSrc, postFragmentSrc)
            postProgramOk = true
        } catch (e: GlUtil.ShaderCompileException) {
            onError("MilkDrop unavailable on this GPU: ${e.message}")
            return
        }
        postLocs.clear()
        val ids = IntArray(1)
        GLES30.glGenVertexArrays(1, ids, 0)
        postVao = ids[0]
    }

    override fun resize(
        width: Int,
        height: Int,
    ) {
        this.width = width
        this.height = height
    }

    private fun effectiveWindowWidth(): Int = if (windowWidth > 1) windowWidth else width

    private fun effectiveWindowHeight(): Int = if (windowHeight > 1) windowHeight else height

    private fun ensureEngine() {
        val w = effectiveWindowWidth()
        val h = effectiveWindowHeight()
        if (w <= 1 || h <= 1) return
        if (handle == 0L) {
            handle = MilkdropEngine.nativeCreate()
            if (handle == 0L) {
                if (!reportedCreateFailure) {
                    reportedCreateFailure = true
                    onError("projectM engine failed to initialize (adb logcat -s milkdrop-jni)")
                }
                return
            }
            lastPresetPath?.let { pendingPresetPath = it }
            engineWidth = 0
        }
        if (engineWidth != w || engineHeight != h) {
            MilkdropEngine.nativeResize(handle, w, h)
            engineWidth = w
            engineHeight = h
        }
    }

    private fun ensureFrameTexture() {
        val w = engineWidth
        val h = engineHeight
        if (w <= 1 || h <= 1) return
        if (frameTex != 0 && texWidth == w && texHeight == h) return
        releaseFrameTexture()
        val ids = IntArray(1)
        GLES30.glGenTextures(1, ids, 0)
        frameTex = ids[0]
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, frameTex)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MIN_FILTER, GLES30.GL_LINEAR)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MAG_FILTER, GLES30.GL_LINEAR)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_S, GLES30.GL_CLAMP_TO_EDGE)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_T, GLES30.GL_CLAMP_TO_EDGE)
        GLES30.glTexImage2D(
            GLES30.GL_TEXTURE_2D,
            0,
            GLES30.GL_RGBA8,
            w,
            h,
            0,
            GLES30.GL_RGBA,
            GLES30.GL_UNSIGNED_BYTE,
            null,
        )
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, 0)
        texWidth = w
        texHeight = h
    }

    override fun update(
        features: AudioFeatures,
        dt: Float,
    ) {
        val p = sceneParams
        rotationAngle = (rotationAngle + p.rotation * dt) % TWO_PI
        zoomPhase = if (p.endlessZoom) (zoomPhase + p.endlessZoomSpeed * dt) % 1f else 0f
        if (p.colorCycle) cyclePhase = (cyclePhase + p.cycleSpeed * dt) % 1f
        beatPulse = maxOf(LiveSignal.hit(features), beatPulse - dt * 3f).coerceAtLeast(0f)
        if (handle == 0L) return
        if (pcmCount > 0) {
            MilkdropEngine.nativeAddPcmMono(handle, pcmBuffer, pcmCount)
            pcmCount = 0
        } else {
            MilkdropEngine.nativeAddPcmMono(handle, features.waveform, features.waveform.size)
        }
    }

    override fun draw(timeSeconds: Float) {
        if (!postProgramOk) return
        GLES30.glGetIntegerv(GLES30.GL_DRAW_FRAMEBUFFER_BINDING, prevFbo, 0)
        ensureEngine()
        ensureFrameTexture()
        if (handle == 0L || frameTex == 0) return
        val now = SystemClock.elapsedRealtime()
        pendingPresetPath?.let { path ->
            if (now - lastLoadMs >= LOAD_DEBOUNCE_MS) {
                pendingPresetPath = null
                lastLoadMs = now
                val dir = File(path).parent ?: "/"
                // The per-preset link directory goes FIRST: it is where the app materializes
                // this preset's texture resolution (renames undone, substitutions, manual
                // choices - see MilkTextureLinks), and search order is the only precedence
                // projectM has, so first is what lets a per-preset choice beat a same-named
                // file in the shared folder.
                val stem = File(path).nameWithoutExtension
                val dirs = mutableListOf("$dir/textures/.links/$stem", dir, "$dir/textures")
                sharedTextureDir?.let { dirs += it }
                MilkdropEngine.nativeSetTexturePaths(handle, dirs.toTypedArray())
                MilkdropEngine.nativeLoadPreset(handle, path, sceneParams.milkdropBlendPresets)
                val error = MilkdropEngine.nativeGetLastError()
                onError(error)
                if (error == null) {
                    lastPresetPath = path
                    onPresetLoaded(path)
                }
            }
        }
        val p = sceneParams
        MilkdropEngine.nativeSetBeatSensitivity(handle, (0.2f + p.beatResponse).coerceIn(0.2f, 3f))

        MilkdropEngine.nativeRender(handle)
        MilkdropEngine.nativeGetLastError()?.let(onError)

        GLES30.glGetIntegerv(GLES30.GL_READ_FRAMEBUFFER_BINDING, prevReadFbo, 0)
        GLES30.glBindFramebuffer(GLES30.GL_READ_FRAMEBUFFER, 0)
        GLES30.glReadBuffer(GLES30.GL_BACK)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, frameTex)
        GLES30.glCopyTexSubImage2D(GLES30.GL_TEXTURE_2D, 0, 0, 0, 0, 0, texWidth, texHeight)
        diagnoseBlackFrame()
        GLES30.glBindFramebuffer(GLES30.GL_READ_FRAMEBUFFER, prevReadFbo[0])

        var drained = 0
        while (GLES30.glGetError() != GLES30.GL_NO_ERROR && drained < 8) drained++
        GlUtil.resetFrameState()
        GLES30.glBindFramebuffer(GLES30.GL_DRAW_FRAMEBUFFER, prevFbo[0])
        GLES30.glViewport(0, 0, width, height)

        GLES30.glDisable(GLES30.GL_BLEND)
        GLES30.glDisable(GLES30.GL_DEPTH_TEST)
        GLES30.glUseProgram(postProgram)
        GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, frameTex)
        setUniform("uTex", 0)
        setUniform1f("uZoom", p.zoom * (1f + beatPulse * p.beatResponse * 0.08f))
        setUniform1f("uRotation", rotationAngle)
        setUniform1f("uZoomPhase", zoomPhase)
        setUniform1f("uMirrorX", if (p.mirror) 1f else 0f)
        setUniform1f("uPalBase", p.paletteBase)
        setUniform1f("uPalSpan", CompositeGrade.paletteSpan(p.hueRange, p.paletteRange))
        setUniform1f("uPalTint", CompositeGrade.paletteTintAmount(p.milkdropPaletteTint))
        setUniform1f("uHue", p.colorShift + cyclePhase)
        setUniform1f("uSat", p.saturation)
        setUniform1f("uBright", p.brightness)
        setUniform1f("uContrast", p.contrast)
        setUniform1f("uGamma", p.gamma)
        setUniform1f("uInvert", if (p.invert) 1f else 0f)
        setUniform1f("uIntensity", p.intensity)
        GLES30.glBindVertexArray(postVao)
        GLES30.glDrawArrays(GLES30.GL_TRIANGLES, 0, 3)
        GLES30.glBindVertexArray(0)
        GLES30.glUseProgram(0)
    }

    private fun setUniform(
        name: String,
        value: Int,
    ) {
        GLES30.glUniform1i(postLocs.getOrPut(name) { GLES30.glGetUniformLocation(postProgram, name) }, value)
    }

    private fun setUniform1f(
        name: String,
        value: Float,
    ) {
        GLES30.glUniform1f(postLocs.getOrPut(name) { GLES30.glGetUniformLocation(postProgram, name) }, value)
    }

    private fun diagnoseBlackFrame() {
        if (diagDone || diagFrames >= DIAG_FRAMES) return
        diagFrames++
        if (diagFrames <= DIAG_WARMUP) return
        val px = diagPixels
        px.position(0)
        GLES30.glReadPixels(texWidth / 2, texHeight / 2, 1, 1, GLES30.GL_RGBA, GLES30.GL_UNSIGNED_BYTE, px)
        px.position(4)
        GLES30.glReadPixels(texWidth / 3, texHeight / 3, 1, 1, GLES30.GL_RGBA, GLES30.GL_UNSIGNED_BYTE, px)
        val sawLight = PROBE_BYTES.any { (px.get(it).toInt() and 0xFF) > 8 }
        if (sawLight) {
            diagDone = true
        } else if (diagFrames >= DIAG_FRAMES) {
            diagDone = true
            onError(
                "MilkDrop diagnostic: the engine painted a black frame for " +
                    "$DIAG_FRAMES frames at ${texWidth}x$texHeight " +
                    "(preset=${lastPresetPath?.substringAfterLast('/') ?: "idle"}). " +
                    "adb logcat -s milkdrop-jni for the native side.",
            )
        }
    }

    private fun releaseFrameTexture() {
        if (frameTex != 0) GLES30.glDeleteTextures(1, intArrayOf(frameTex), 0)
        frameTex = 0
        texWidth = 0
        texHeight = 0
    }

    override fun release() {
        if (handle != 0L) {
            MilkdropEngine.nativeDestroy(handle)
            handle = 0
        }
        engineWidth = 0
        engineHeight = 0
        releaseFrameTexture()
        if (postProgram != 0) GLES30.glDeleteProgram(postProgram)
        postProgram = 0
        postProgramOk = false
        if (postVao != 0) GLES30.glDeleteVertexArrays(1, intArrayOf(postVao), 0)
        postVao = 0
        postLocs.clear()
    }
}
