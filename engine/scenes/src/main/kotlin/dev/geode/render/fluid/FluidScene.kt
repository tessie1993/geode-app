package dev.geode.render.fluid

import android.content.Context
import android.opengl.GLES30
import dev.geode.analysis.AudioFeatures
import dev.geode.render.LiveSignal
import dev.geode.render.scene.GlUtil
import dev.geode.render.scene.ParticleLook
import dev.geode.render.scene.SceneIds

internal class FluidScene(
    context: Context,
) : FluidSceneBase(TIME_WRAP_SECONDS) {
    override val id: String = SceneIds.FLUID

    private companion object {
        const val TIME_WRAP_SECONDS = 7100f

        const val IDLE_WRAP_SECONDS = 628.31853f
    }

    private val sim = FluidSim(context)
    private val look = FluidLook(context)
    private val particles = FluidParticles(context)
    private val emitters = FluidEmitters().also { it.choreography = choreography }

    private val splats = ArrayList<FluidSim.Splat>()

    private var appliedParticleSide = 0

    val velocityTexture: Int get() = sim.velocityTex
    val simAvailable: Boolean get() = sim.available

    fun setInjectionShaders(
        forceSrc: String?,
        dyeSrc: String?,
    ) = sim.setInjectionShaders(forceSrc, dyeSrc)

    override fun init() {
        sim.onShaderError = { onShaderError(it) }
        sim.create()
        choreography.reset()
        if (sim.available) {
            look.create(sim.texFormats)
            appliedTier = -1
            appliedParticleSide = 0
            applyQualityTier()
        } else {
            onShaderError("Fluid style unavailable: this GPU can't render half-float buffers")
        }
    }

    override fun resize(
        width: Int,
        height: Int,
    ) {
        if (sim.resize(width, height)) particles.invalidateSeed()
        look.resize(width, height)
    }

    override fun tierApplied(): Boolean = appliedParticleSide != 0

    override fun onApplyQualityTier(
        index: Int,
        userChanged: Boolean,
    ) {
        val tier = FluidQuality.tier(index)
        sim.applyResolution(tier.simRes, tier.dyeRes)
        val recreateParticles =
            appliedParticleSide == 0 || (userChanged && appliedParticleSide != tier.particleSide)
        if (recreateParticles) {
            appliedParticleSide = tier.particleSide
            particles.create(tier.particleSide * tier.particleSide, sim.texFormats)
        }
    }

    private var idlePhase = 0f
    private var diagFrames = 0
    private var dyeProbed = false

    override fun idleFeatures(dt: Float): AudioFeatures {
        idlePhase = (idlePhase + dt) % IDLE_WRAP_SECONDS
        val t = idlePhase
        val bass = 0.18f + 0.12f * kotlin.math.sin(t * 0.7f)
        val mid = 0.15f + 0.10f * kotlin.math.sin(t * 1.1f + 1.7f)
        val treble = 0.05f + 0.04f * kotlin.math.sin(t * 1.9f + 3.1f)
        fillIdleBands(t, 0.08f)
        return idleAudioFeatures(bass, mid, treble, rms = 0.2f)
    }

    override fun draw(timeSeconds: Float) {
        if (!sim.available) return
        GlUtil.resetFrameState()
        val p = params
        val f = scaledFeatures()

        saveGlState()

        autoQualityTick()

        val energy = f.rms.coerceIn(0f, 1f)
        val pcmKick = pcmStrike.coerceIn(0f, 1f)
        sim.pressureIterations = p.fluidIterations.coerceIn(8, 40)
        sim.pressureDamp = p.fluidPressure.coerceIn(0f, 1f)
        sim.velocityDissipation = p.fluidVelocityDissipation.coerceIn(0f, 4f)
        sim.curlStrength = p.fluidCurl.coerceIn(0f, 50f) * (1f + p.fluidCurlAudio * f.mid + pcmKick * 0.5f)
        sim.densityDissipation =
            p.fluidDensityDissipation.coerceIn(0f, 4f) *
            (1f + p.fluidFadeAudio * (1f - energy))
        sim.chromaticAging = p.fluidChromaticAging.coerceIn(0f, 1f)
        sim.audioBass = f.bass
        sim.audioMid = f.mid
        sim.audioTreble = f.treble
        sim.audioEnergy = energy
        sim.audioBeat = LiveSignal.hit(f)
        sim.timeSeconds = time

        configureChoreography()

        emitters.applyParams(p)
        emitters.paletteCycleSpeed = FluidHue.paletteCycleSpeed(p.fluidPaletteCycleSpeed)
        emitters.forceScale = p.fluidSplatForce.coerceIn(0f, 3f) * (1f + pcmKick * 0.5f)
        val simDt = lastDt.coerceIn(0f, 1f / 30f)
        val hueBase = FluidHue.base(p.paletteBase)
        val hueSpan = FluidHue.span(p.hueRange, p.paletteRange)
        choreography.tick(f, simDt, sim.aspect)
        emitters.tick(f, simDt, sim.aspect, hueBase, hueSpan, splats)
        for (i in splats.indices) sim.queueSplat(splats[i])
        sim.step(simDt)
        if (diagFrames < 3) {
            val err = GLES30.glGetError()
            if (err != GLES30.GL_NO_ERROR) {
                android.util.Log.w("FluidSim", "glError after step frame $diagFrames: 0x${Integer.toHexString(err)}")
            }
            diagFrames++
            if (diagFrames == 3) android.util.Log.i("FluidSim", "first frames stepped clean (no GL errors)")
        }
        if (particles.available && p.fluidParticlesEnabled) {
            applyChoreographyTo(particles)
            particles.step(simDt, sim.velocityTex, sim.aspect, sim.flowScale, timeSeconds = time)
        }
        look.bloomIntensity =
            p.fluidBloomIntensity.coerceIn(0.1f, 2f) * (0.6f + p.fluidBloomAudio * energy)
        look.bloomThreshold = p.fluidBloomThreshold.coerceIn(0f, 1f)
        look.sunraysWeight = p.fluidSunraysWeight.coerceIn(0.3f, 1f)
        if (p.fluidDyeEnabled) look.process(sim.dyeTex, p.fluidBloom, p.fluidSunrays)
        pendingFeatures = null
        if (!dyeProbed && time > 1.5f) {
            dyeProbed = true
            android.util.Log.i("FluidSim", "dye liveness: ${sim.probeDyeMax()}")
        }

        restoreFramebufferAndViewport()
        if (p.fluidDyeEnabled) {
            if (look.available) {
                look.drawDisplay(
                    dyeTex = sim.dyeTex,
                    shadingOn = p.fluidShading,
                    bloomOn = p.fluidBloom,
                    sunraysOn = p.fluidSunrays,
                    viewportW = savedViewportWidth.coerceAtLeast(1),
                    viewportH = savedViewportHeight.coerceAtLeast(1),
                )
            } else {
                sim.drawDisplay()
            }
        }
        if (particles.available && p.fluidParticlesEnabled) {
            particles.draw(
                aspect = sim.aspect,
                pointScale = (1.5f * p.particleSize.coerceIn(0.2f, 3f)) * viewportDpiScale(),
                hueBase = hueBase,
                hueSpan = hueSpan,
                brightness =
                    0.55f * p.fluidParticleBrightness.coerceIn(0f, 2f) *
                        (0.3f + p.density.coerceIn(0f, 1.5f)),
                shape = p.particleShape.toFloat(),
                glow = ParticleLook.glow(p.bloom),
                timeSeconds = timeSeconds,
            )
        }
        restoreBlend()
    }

    override fun release() {
        particles.release()
        look.release()
        sim.release()
        appliedTier = -1
        appliedParticleSide = 0
    }
}
