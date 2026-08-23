package dev.geode.render.fluid

import android.opengl.GLES30
import dev.geode.analysis.AudioFeatures
import dev.geode.render.ThermalGovernor
import dev.geode.render.scene.PcmPulse
import dev.geode.render.scene.PcmSink
import dev.geode.render.scene.Scene
import dev.geode.render.scene.SceneParams

internal abstract class FluidSceneBase(
    private val timeWrapSeconds: Float,
) : Scene,
    PcmSink {
    protected val choreography = FluidChoreography()
    protected val audioDrive = FluidAudioDrive()
    protected val monitor = PerformanceMonitor()

    private val pcmPulse = PcmPulse()
    protected var pcmStrike = 0f
        private set

    protected var params = SceneParams()
        private set

    protected var time = 0f
    protected var lastDt = 1f / 60f
    protected var pendingFeatures: AudioFeatures? = null
    protected var lastFeatures: AudioFeatures? = null
    protected var featuresAgeSec = 0f

    protected var autoDowngrade = 0
    protected var lastUserQuality = -1
    protected var appliedTier = -1

    var onShaderError: (String?) -> Unit = {}

    private val prevFbo = IntArray(1)
    private val prevViewport = IntArray(4)
    private val prevBlendFunc = IntArray(4)
    private var blendWas = false

    private val spawnPack = FloatArray(FluidChoreography.MAX_SPAWN * 4)
    private val catchPack = FloatArray(FluidChoreography.MAX_CATCH * 4)

    private val idleBands = FloatArray(16)
    private val idleWaveform = FloatArray(64)

    override fun setParams(params: SceneParams) {
        this.params = params
    }

    override fun acceptPcm(
        samples: FloatArray,
        count: Int,
    ) = pcmPulse.accept(samples, count)

    override fun update(
        features: AudioFeatures,
        dt: Float,
    ) {
        time = (time + dt) % timeWrapSeconds
        lastDt = dt
        pcmStrike = pcmPulse.tick(dt)
        pendingFeatures = features
        lastFeatures = features
        featuresAgeSec = 0f
    }

    protected fun tickPcm(dt: Float): Float = pcmPulse.tick(dt)

    protected abstract fun idleFeatures(dt: Float): AudioFeatures

    protected fun scaledFeatures(): AudioFeatures {
        featuresAgeSec = (featuresAgeSec + lastDt).coerceAtMost(1f)
        return audioDrive.scaled(
            pendingFeatures
                ?: lastFeatures.takeIf { featuresAgeSec < 0.25f }
                ?: idleFeatures(lastDt),
            params.audioDrive,
        )
    }

    protected val isIdle: Boolean
        get() = pendingFeatures == null && featuresAgeSec >= 0.25f

    protected fun fillIdleBands(
        t: Float,
        amp: Float,
    ) {
        for (i in idleBands.indices) idleBands[i] = 0.1f + amp * kotlin.math.sin(t * (0.5f + i * 0.13f))
    }

    protected fun idleAudioFeatures(
        bass: Float,
        mid: Float,
        treble: Float,
        rms: Float,
    ): AudioFeatures =
        AudioFeatures(
            bands = idleBands,
            waveform = idleWaveform,
            rms = rms,
            bass = bass.coerceAtLeast(0f),
            mid = mid.coerceAtLeast(0f),
            treble = treble.coerceAtLeast(0f),
            beat = false,
        )

    protected open fun tierApplied(): Boolean = true

    protected abstract fun onApplyQualityTier(
        index: Int,
        userChanged: Boolean,
    )

    protected fun applyQualityTier() {
        val userChanged = params.fluidQuality != lastUserQuality
        if (userChanged) {
            lastUserQuality = params.fluidQuality
            autoDowngrade = 0
            monitor.reset()
        }
        val idx = FluidQuality.effectiveIndex(params.fluidQuality, if (params.fluidAutoQuality) autoDowngrade else 0)
        if (idx == appliedTier && tierApplied()) return
        appliedTier = idx
        onApplyQualityTier(idx, userChanged)
    }

    protected fun autoQualityTick() {
        if (params.fluidAutoQuality) {
            // The rate the display is being ASKED for, so a deliberate cap — a wallpaper paced at
            // 30, or the thermal governor's own last-resort cap — is not read as a device failing
            // to keep up and charged as two quality tiers within three seconds.
            monitor.pacedFps = ThermalGovernor.pacedFps
            val severity = monitor.onFrame(lastDt)
            if (severity > 0) {
                autoDowngrade += severity
                monitor.reset()
            }
        }
        applyQualityTier()
    }

    protected fun configureChoreography() {
        val p = params
        choreography.path = p.fluidSpawnPath.coerceIn(0, FluidChoreography.PATH_LABELS.size - 1)
        choreography.spawnCount = p.fluidSpawnPoints.coerceIn(1, FluidChoreography.MAX_SPAWN)
        choreography.catchCount = p.fluidCatchPoints.coerceIn(0, FluidChoreography.MAX_CATCH)
        choreography.progressionAmount = p.fluidSpawnProgress.coerceIn(0f, 1f)
        choreography.speed = FluidChoreography.sceneSpeed(p.speed)
    }

    protected fun applyChoreographyTo(particles: FluidParticles) {
        val p = params
        particles.drag = p.fluidParticleDrag.coerceIn(0.02f, 1f)
        particles.life = p.fluidParticleLife.coerceIn(1f, 20f)
        choreography.packSpawns(spawnPack)
        choreography.packCatches(
            catchPack,
            pull = p.fluidCatchPull.coerceIn(0f, 3f),
            captureRadius = p.fluidCatchRadius.coerceIn(0.03f, 0.3f),
        )
        particles.setChoreography(spawnPack, choreography.spawnCount, catchPack, choreography.catchCount)
    }

    protected fun saveFramebufferAndViewport() {
        GLES30.glGetIntegerv(GLES30.GL_FRAMEBUFFER_BINDING, prevFbo, 0)
        GLES30.glGetIntegerv(GLES30.GL_VIEWPORT, prevViewport, 0)
    }

    protected fun saveGlState() {
        saveFramebufferAndViewport()
        GLES30.glGetIntegerv(GLES30.GL_BLEND_SRC_RGB, prevBlendFunc, 0)
        GLES30.glGetIntegerv(GLES30.GL_BLEND_DST_RGB, prevBlendFunc, 1)
        GLES30.glGetIntegerv(GLES30.GL_BLEND_SRC_ALPHA, prevBlendFunc, 2)
        GLES30.glGetIntegerv(GLES30.GL_BLEND_DST_ALPHA, prevBlendFunc, 3)
        blendWas = GLES30.glIsEnabled(GLES30.GL_BLEND)
    }

    protected fun restoreFramebufferAndViewport() {
        GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, prevFbo[0])
        GLES30.glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3])
    }

    protected fun restoreBlend() {
        if (blendWas) GLES30.glEnable(GLES30.GL_BLEND) else GLES30.glDisable(GLES30.GL_BLEND)
        GLES30.glBlendFuncSeparate(prevBlendFunc[0], prevBlendFunc[1], prevBlendFunc[2], prevBlendFunc[3])
    }

    protected val savedViewportWidth: Int
        get() = prevViewport[2]

    protected val savedViewportHeight: Int
        get() = prevViewport[3]

    protected fun viewportDpiScale(): Float = (savedViewportHeight.coerceAtLeast(1) / 1080f).coerceIn(0.75f, 2.5f)
}
