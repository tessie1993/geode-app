package dev.geode.render.offscreen

import android.content.Context
import dev.geode.analysis.FeatureTimeline
import dev.geode.render.AdsrConfig
import dev.geode.render.LfoConfig
import dev.geode.render.SceneFactory
import dev.geode.render.TransitionStyle
import dev.geode.render.bridge.NativeViz
import dev.geode.render.scene.SceneParams
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
    /**
     * Full-frame ARGB overlay pixels (from `Bitmap.getPixels`, sized [width]x[height]) for the
     * frame at a track position (ms, measured the same way [FeatureTimeline.featuresAt] is:
     * [rangeStartMs] plus the frame's offset into the export); null for a position draws none for
     * that frame. Consulted once per frame in [OffscreenSceneRenderer.renderFrame]; returning the
     * same array instance across calls (a fixed overlay, or an unchanged lyric line) skips the
     * native re-upload.
     */
    val overlay: ((positionMs: Long) -> IntArray?)? = null,
    // W02: the background image behind the scene, decoded by the caller at [width]x[height] and
    // latched once in [OffscreenSceneRenderer.prepare] - see NativeViz.setUnderlay.
    val underlay: OffscreenUnderlay? = null,
)

/**
 * A background image ready to latch into the export's native renderer; see
 * [OffscreenRenderSpec.underlay].
 *
 * A plain class, not a data class: [pixels] is a large array, and the compiler-generated
 * equals()/hashCode() a data class would get compare it by reference anyway, which is misleading
 * (see [dev.geode.render.scene.PcmChunk] for the same reasoning).
 */
class OffscreenUnderlay(
    val pixels: IntArray,
    val width: Int,
    val height: Int,
    val blend: Int,
    val amount: Float,
)

/**
 * Draws an analysed track to a GL target, one frame at a time, through its own native renderer.
 *
 * Callers own the EGL context, the surface and whatever they do with the rendered frames; the
 * scene, its simulations and the modulation engines live inside `libgeode.so`. Not thread-safe,
 * and every call must happen on the thread holding the GL context.
 */
class OffscreenSceneRenderer(
    private val context: Context,
    private val sceneFactory: SceneFactory,
    private val timeline: FeatureTimeline,
    private val spec: OffscreenRenderSpec,
) {
    private var native: NativeViz? = null

    // Tracks the last overlay array uploaded to the native renderer, by reference rather than
    // content, so renderFrame() only calls setOverlay() when spec.overlay actually returns a new
    // instance - see spec.overlay's doc. Starting at null means a spec with no overlay (or one
    // whose first frame has nothing to draw) never calls setOverlay() at all, matching the old
    // latch-once behaviour for the art/title-only case.
    private var lastOverlayPixels: IntArray? = null

    /** Builds the native renderer and its scene; must run with the target GL context current. */
    fun prepare() {
        val viz = NativeViz(context)
        check(viz.create()) { "native renderer could not be created" }
        // Pinned to the full tier: an export renders as fast as the encoder takes frames, so its
        // wall-clock frame time says nothing about the device.
        viz.setOffscreen(true)
        viz.surfaceCreated()
        viz.surfaceChanged(spec.width, spec.height)
        check(viz.setScene(sceneFactory.sceneId)) { "native renderer cannot draw \"${sceneFactory.sceneId}\"" }
        sceneFactory.milkPresetPath?.let { viz.loadMilkPreset(it) }
        viz.setTransition(TransitionStyle.CUT.name.lowercase(), 0L)
        viz.setReducedMotion(spec.reducedMotion)
        if (spec.lfoConfigs.isNotEmpty()) viz.setLfoConfigs(spec.lfoConfigs)
        if (spec.adsrConfigs.isNotEmpty()) viz.setAdsrConfigs(spec.adsrConfigs)
        // The overlay itself is latched per-frame in renderFrame(), since spec.overlay may vary
        // with position (a lyric line, or a watermark once enabled); this only builds the renderer.
        spec.underlay?.let { viz.setUnderlay(it.pixels, it.width, it.height, it.blend, it.amount) }
        native = viz
    }

    /**
     * Renders frame [frame] of the sequence into [targetFbo] (0 = the default framebuffer).
     *
     * [prepare] must have run first. The caller presents the result — swapping an encoder
     * surface, reading pixels back, whatever it needs.
     */
    fun renderFrame(
        frame: Int,
        targetFbo: Int = 0,
    ) {
        val viz = checkNotNull(native) { "renderFrame() before prepare()" }
        val fps = spec.fps
        val timeMs = frame * 1000L / fps
        val nextTimeMs = (frame + 1) * 1000L / fps
        val absoluteMs = spec.rangeStartMs + timeMs
        val features = timeline.featuresAt(absoluteMs, nextTimeMs - timeMs)
        // Quality never adapts downward in an export; the thermal half is the offscreen pin above.
        val p = (spec.paramsAt?.invoke(timeMs) ?: spec.baseParams).copy(fluidAutoQuality = false)
        viz.setParams(p)
        viz.setFeatures(features)
        val overlayPixels = spec.overlay?.invoke(absoluteMs)
        if (overlayPixels !== lastOverlayPixels) {
            viz.setOverlay(overlayPixels, spec.width, spec.height)
            lastOverlayPixels = overlayPixels
        }
        viz.render(timeMs / 1000.0, targetFbo)
    }

    /** Frees the native renderer. Safe to call more than once. */
    fun release() {
        bestEffort(TAG, "native.destroy()") { native?.destroy() }
        native = null
    }

    private companion object {
        const val TAG = "OffscreenSceneRenderer"
    }
}
