package dev.geode.render.offscreen

import android.content.Context
import android.opengl.GLES30
import dev.geode.analysis.FeatureTimeline
import dev.geode.render.AdsrConfig
import dev.geode.render.AdsrEngine
import dev.geode.render.LfoConfig
import dev.geode.render.LfoEngine
import dev.geode.render.SceneFactory
import dev.geode.render.VisualSafety
import dev.geode.render.fluid.CurlFlowMath
import dev.geode.render.scene.Scene
import dev.geode.render.scene.SceneParams
import dev.geode.render.scene.applyBandGains
import dev.geode.util.bestEffort

/**
 * Everything needed to render one offscreen sequence: the frame grid and the parameter
 * automation that drives it.
 *
 * [paramsAt] supplies the keyframed parameters for a timeline position, measured from the start
 * of the exported range; when it is null every frame uses [baseParams].
 */
data class OffscreenRenderSpec(
    val width: Int,
    val height: Int,
    val fps: Int,
    val totalFrames: Int,
    val rangeStartMs: Long,
    val baseParams: SceneParams,
    val lfoConfigs: List<LfoConfig> = emptyList(),
    val adsrConfigs: List<AdsrConfig> = emptyList(),
    val reducedMotion: Boolean = false,
    val paramsAt: ((Long) -> SceneParams)? = null,
)

/**
 * Draws an analysed track to the current GL surface, one frame at a time.
 *
 * This is the whole offscreen half of the render engine behind three calls — [prepare],
 * [renderFrame] and [release]. Callers own the EGL context, the surface and whatever they do
 * with the rendered frames (the app encodes them to video); they never see the scenes, the
 * fluid simulations, the compositor or the modulation engines that produce them.
 *
 * Not thread-safe, and every call must happen on the thread holding the GL context.
 */
class OffscreenSceneRenderer(
    private val context: Context,
    private val sceneFactory: SceneFactory,
    private val timeline: FeatureTimeline,
    private val spec: OffscreenRenderSpec,
) {
    private val lfoEngine = LfoEngine()
    private val adsrEngine = AdsrEngine()
    private val rippleDrops = dev.geode.render.fluid.RippleOverlayDrops()

    private var scene: Scene? = null
    private var compositor: OffscreenCompositor? = null
    private var flowField: dev.geode.render.fluid.FlowField? = null
    private var rippleOverlay: dev.geode.render.fluid.RippleSim? = null

    private var isShaderScene = false
    private var isProjectM = false
    private var canvasPersists = false
    private var isCurlFlow = false
    private var isBeam = false
    private var fluidScene: dev.geode.render.fluid.FluidScene? = null
    private var thermalSuppressed = false

    /**
     * Builds the scene and every effect the sequence turns out to need.
     *
     * Must be called with the target GL context current, before the first [renderFrame].
     */
    fun prepare() {
        // Held for the whole sequence, not per frame: an export renders as fast as the encoder
        // will take frames, so wall-clock frame time here measures the encoder and says nothing
        // about the device. Left running, the thermal governor would read a 30 fps render
        // delivered at 12 fps of wall clock as a permanent deficit and shed quality out of the
        // file the user is waiting on. Paired with [release]; see ThermalGovernor for the whole
        // argument, and [renderFrame] for the fluid ladder's half of the same suppression.
        if (!thermalSuppressed) {
            thermalSuppressed = true
            dev.geode.render.ThermalGovernor.beginOffscreenRender()
        }
        val created = sceneFactory.create().also { scene = it }
        created.init()
        created.resize(spec.width, spec.height)
        GLES30.glViewport(0, 0, spec.width, spec.height)

        isShaderScene = created is dev.geode.render.scene.ShaderScene
        isProjectM = created is dev.geode.render.scene.MilkdropScene
        isCurlFlow = created is dev.geode.render.fluid.CurlFlowScene
        isBeam = created is dev.geode.render.scene.BeamScene
        canvasPersists = isCurlFlow || isBeam
        fluidScene = created as? dev.geode.render.fluid.FluidScene

        compositor = OffscreenCompositor(context, spec.width, spec.height)

        if (spec.lfoConfigs.isNotEmpty()) lfoEngine.configs = spec.lfoConfigs
        if (spec.adsrConfigs.isNotEmpty()) adsrEngine.configs = spec.adsrConfigs

        // A flow field or ripple sim costs a full simulation per frame, so only stand one up if
        // some frame in the sequence actually switches it on — and never when the scene already
        // provides its own.
        val use = scanEffectUse()
        if (use.flowField && fluidScene == null) {
            flowField =
                dev.geode.render.fluid.FlowField(context).also {
                    it.create()
                    it.resize(spec.width, spec.height)
                }
        }
        if (use.rippleOverlay && created !is dev.geode.render.fluid.WaterScene) {
            rippleOverlay =
                dev.geode.render.fluid.RippleSim(context).also {
                    it.create()
                    it.applyResolution(RIPPLE_OVERLAY_RES)
                    it.resize(spec.width, spec.height)
                }
        }
    }

    /**
     * Renders frame [frame] of the sequence to the default framebuffer.
     *
     * [prepare] must have run first. The caller presents the result — swapping an encoder
     * surface, reading pixels back, whatever it needs.
     */
    fun renderFrame(frame: Int) {
        val scene = checkNotNull(scene) { "renderFrame() before prepare()" }
        val fx = checkNotNull(compositor) { "renderFrame() before prepare()" }
        dev.geode.render.scene.GlUtil.resetFrameState()

        val fps = spec.fps
        val dt = 1f / fps
        val timeMs = frame * 1000L / fps
        val nextTimeMs = (frame + 1) * 1000L / fps
        // featuresAt, not progressionAt: nothing in the render path reads `progress` or
        // `sectionIndex` any more, so the export replays only the per-frame signal — the same
        // transients, level, brightness and bands the live path reacts to.
        val features = timeline.featuresAt(spec.rangeStartMs + timeMs, nextTimeMs - timeMs)

        val envValues = adsrEngine.tick(dt, features)
        val (envRate, envDepth) = AdsrEngine.lfoOffsets(adsrEngine.configs, envValues)
        val lfoValues = lfoEngine.tick(dt, features, envRate, envDepth)

        var p = spec.paramsAt?.invoke(timeMs) ?: spec.baseParams
        p = LfoEngine.apply(p, lfoEngine.configs, lfoValues)
        p = AdsrEngine.apply(p, adsrEngine.configs, envValues)
        p = VisualSafety.apply(p, spec.reducedMotion)
        // An offscreen render has no frame budget to protect, so quality never adapts downward.
        // The thermal half of the same rule is the suppression [prepare] installs — both have to
        // hold, or the exported file records whatever the phone happened to feel like that day.
        p = p.copy(fluidAutoQuality = false)

        val banded = applyBandGains(features, p)
        scene.setParams(p)
        scene.update(banded, dt)

        val flow = flowField
        if (p.flowEnabled && flow != null && flow.available) {
            flow.step(banded, dt, p)
            if (scene is dev.geode.render.scene.ShaderScene) scene.setFlow(flow.velocityTex, p.flowStrength)
        }

        val ripple = rippleOverlay
        val rippleOn = p.rippleOverlayEnabled && ripple != null && ripple.available
        if (rippleOn && ripple != null) {
            ripple.waveSpeed = 1.2f * p.waterWaveSpeed.coerceIn(0.2f, 2f)
            ripple.damping = p.waterDamping.coerceIn(0.9f, 0.999f)
            rippleDrops.tick(banded, ripple.aspect) { x, y, radius, amp -> ripple.queueDrop(x, y, radius, amp) }
            ripple.step(dt)
        }

        fx.bindSceneTarget()
        if (canvasPersists && frame > 0) {
            // Scenes that paint onto a persistent canvas fade the previous frame instead of
            // clearing it; the retention curve differs per scene family.
            val fadeParams =
                when {
                    isCurlFlow -> p.copy(trailLength = CurlFlowMath.retention(p.trailLength, p.trails))
                    isBeam -> p.copy(trailLength = beamRetention(p.trailLength))
                    else -> p
                }
            fx.fadeSceneTargetWarp(fadeParams, fx.sceneFbo, fx.width, fx.height, timeMs / 1000f, dt)
        } else {
            GLES30.glClearColor(0f, 0f, 0f, 1f)
            GLES30.glClear(GLES30.GL_COLOR_BUFFER_BIT)
        }
        scene.draw(timeMs / 1000f)

        val sim = fluidScene
        val flowTex =
            when {
                !p.flowEnabled -> 0
                sim != null && sim.simAvailable -> sim.velocityTexture
                flow != null && flow.available -> flow.velocityTex
                else -> 0
            }
        val rippleTex = if (rippleOn && ripple != null) ripple.heightTex else 0
        fx.composite(
            timeSeconds = timeMs / 1000f,
            dtSeconds = dt,
            features = features,
            isShaderScene = isShaderScene,
            isProjectM = isProjectM,
            params = p,
            flowTex = flowTex,
            flowStrength = if (flowTex != 0) p.flowStrength else 0f,
            rippleTex = rippleTex,
            rippleTexelW = if (rippleTex != 0 && ripple != null) ripple.texelW else 0f,
            rippleTexelH = if (rippleTex != 0 && ripple != null) ripple.texelH else 0f,
            rippleStrength = if (rippleTex != 0) p.rippleOverlayStrength.coerceIn(0f, 1f) else 0f,
            rippleSpecular = if (rippleTex != 0) p.rippleOverlaySpecular.coerceIn(0f, 1f) else 0f,
            strobeHz = VisualSafety.strobeHz(),
        )
    }

    /** Frees every GL object this renderer owns. Safe to call more than once. */
    fun release() {
        // Ahead of the GL teardown and behind its own flag, so a second release() — which this
        // method promises to tolerate — cannot hand the governor back a suppression it never took.
        if (thermalSuppressed) {
            thermalSuppressed = false
            dev.geode.render.ThermalGovernor.endOffscreenRender()
        }
        bestEffort(TAG, "scene.release()") { scene?.release() }
        bestEffort(TAG, "flowField.release()") { flowField?.release() }
        bestEffort(TAG, "rippleOverlay.release()") { rippleOverlay?.release() }
        bestEffort(TAG, "compositor.release()") { compositor?.release() }
        scene = null
        flowField = null
        rippleOverlay = null
        compositor = null
    }

    private data class EffectUse(
        val flowField: Boolean,
        val rippleOverlay: Boolean,
    )

    private fun scanEffectUse(): EffectUse {
        val at = spec.paramsAt ?: return EffectUse(spec.baseParams.flowEnabled, spec.baseParams.rippleOverlayEnabled)
        var flow = false
        var ripple = false
        for (frame in 0 until spec.totalFrames) {
            val p = at(frame * 1000L / spec.fps)
            flow = flow || p.flowEnabled
            ripple = ripple || p.rippleOverlayEnabled
            if (flow && ripple) break
        }
        return EffectUse(flow, ripple)
    }

    private companion object {
        const val TAG = "OffscreenSceneRenderer"
        const val RIPPLE_OVERLAY_RES = 256

        fun beamRetention(trailLength: Float): Float = (0.55f + 0.44f * trailLength).coerceIn(0f, 0.99f)
    }
}
