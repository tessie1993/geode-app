package dev.geode.render.offscreen

import android.content.Context
import android.net.Uri
import android.util.Log
import dev.geode.render.SceneFactory
import dev.geode.render.bridge.NativeViz
import dev.geode.render.bridge.StyleSchedule
import dev.geode.render.look.LookApplier
import dev.geode.render.look.LookSnapshot
import dev.geode.render.scene.SceneParams
import dev.geode.util.bestEffort

/**
 * Everything needed to render one offscreen sequence: the frame grid, the [look] the caller pinned
 * (a value: live edits never reach it) and the automation that moves it.
 *
 * [paramsAt] supplies the keyframed parameters for a timeline position, measured from the start
 * of the exported range; when it is null every frame uses the look's params.
 */
data class OffscreenRenderSpec(
    val width: Int,
    val height: Int,
    val fps: Int,
    val totalFrames: Int,
    val rangeStartMs: Long,
    // Its underlay decoded by the caller at [width]x[height]; the renderer adds the export override (LookSnapshot.forExport).
    val look: LookSnapshot,
    val paramsAt: ((Long) -> SceneParams)? = null,
    // The track the range is cut from: the export's own sound engine hears it frame by frame
    // (TrackAudio), the same way the screen's engine hears the ring. Null renders without sound.
    val audioUri: Uri? = null,
    // An export's length: non-null pins the engine settings (macros, Note & key switch, hand mask, director)
    // natively at prepare (NativeViz.pinEngineSettings, #350), so live edits never reach the render; the
    // director's free-taste rule is native. Null follows the process globals (previews, thumbnails).
    val pinDurationMs: Long? = null,
    // Pro or the trial (ProFeature.AUTO_DIRECTOR), for the pinned director; read only with [pinDurationMs].
    val entitled: Boolean = false,
    // Studio 2D: the styles this range switches to (a take's switches, a beat cut), on render time
    // since [rangeStartMs]. The native renderer times them; empty draws the factory's style throughout.
    val styleSchedule: StyleSchedule = StyleSchedule.NONE,
)

/**
 * Draws a track to a GL target, one frame at a time, through its own native renderer.
 *
 * Callers own the EGL context, the surface and whatever they do with the rendered frames; the
 * scene, its simulations and the modulation engines live inside `libgeode.so`. Not thread-safe,
 * and every call must happen on the thread holding the GL context.
 */
class OffscreenSceneRenderer(
    private val context: Context,
    private val sceneFactory: SceneFactory,
    private val spec: OffscreenRenderSpec,
) {
    private var native: NativeViz? = null
    private var audio: TrackAudio? = null

    // The factory's style opens the range (a take's opening style); the schedule switches natively after.
    private val look = spec.look.forExport().copy(sceneId = sceneFactory.sceneId)
    private val looks = LookApplier()

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
        viz.setStyleSchedule(spec.styleSchedule)
        looks.apply(look, viz)
        spec.pinDurationMs?.let { viz.pinEngineSettings(spec.entitled, it) }
        native = viz
        // A track that will not decode still exports, silent to the engine, rather than failing the render.
        audio =
            spec.audioUri?.let { uri ->
                runCatching { TrackAudio(context, uri, spec.rangeStartMs) }
                    .onFailure { Log.w(TAG, "track audio unavailable; the sound engine stays silent", it) }
                    .getOrNull()
            }
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
        // Keyframed params ride in the look (fixed quality, LookSnapshot.forExport); reactSeed among them, so native
        // re-deals the DNA when keyframes change it.
        looks.apply(spec.paramsAt?.let { look.copy(params = it(timeMs)) } ?: look, viz)
        // Exactly the audio up to the end of this frame, then a render steps the engine by 1/fps.
        audio?.deliverUntilMs(nextTimeMs) { pcm, frames, channels, rate -> viz.writeAudio(pcm, frames, channels, rate) }
        viz.render(timeMs / 1000.0, targetFbo)
    }

    /** Frees the native renderer. Safe to call more than once. */
    fun release() {
        bestEffort(TAG, "native.destroy()") { native?.destroy() }
        native = null
        bestEffort(TAG, "audio.close()") { audio?.close() }
        audio = null
    }

    private companion object {
        const val TAG = "OffscreenSceneRenderer"
    }
}
