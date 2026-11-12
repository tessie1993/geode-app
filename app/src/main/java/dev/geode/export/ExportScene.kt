package dev.geode.export

import android.content.Context
import android.content.res.AssetFileDescriptor
import android.net.Uri
import dev.geode.data.LookSnapshot
import dev.geode.engine.bridge.GeodeNative
import dev.geode.util.bestEffort
import dev.geode.viz.NativeViz

/**
 * One export's picture: a native renderer driven by the native export job (`core/output/ExportFrame`), which
 * owns the prepare order, the pre-roll, the track's decode (`core/audio/ingest`: AIFF natively, anything else
 * through AMediaExtractor and AMediaCodec) and each frame's audio and draw. This opens the renderer and the
 * track's descriptor and hands the look over between the job's two prepare steps, because native merges the look's
 * reactions under the hand mask the job's settings pin sets. It decides nothing.
 *
 * Callers own the EGL context and the surface and present what each [renderFrame] drew. Not thread-safe: every
 * call on the thread holding the GL context.
 */
internal class ExportScene(
    private val context: Context,
    private val sceneFactory: SceneFactory,
    private val spec: Spec,
) {
    /**
     * The frame grid, the [look] the caller pinned (a value: live edits never reach it, and its underlay is
     * decoded at [width]x[height]), the track the range is cut from and the render's length (the engine
     * settings are pinned for it, #350).
     */
    class Spec(
        val width: Int,
        val height: Int,
        val fps: Int,
        val rangeStartMs: Long,
        val look: LookSnapshot,
        val audioUri: Uri,
        val durationMs: Long,
        // Pro or the trial (ProFeature.AUTO_DIRECTOR), for the pinned director.
        val entitled: Boolean,
    )

    private val viz = NativeViz(context)
    private var job = 0L

    /** Builds the renderer, its scene and a warm sound engine; must run with the target GL context current. */
    fun prepare() {
        check(viz.create()) { "native renderer could not be created" }
        val (fd, offset, length) = openTrack()
        job = GeodeNative.exportJobCreate(viz.nativeHandle, fd, offset, length, spec.fps, spec.rangeStartMs)
        check(job != 0L) { "export job not created" }
        val sceneId = sceneFactory.sceneId
        val handMask =
            GeodeNative.exportJobBegin(
                job,
                spec.width,
                spec.height,
                sceneId,
                sceneFactory.milkPresetPath,
                spec.entitled,
                spec.durationMs,
            )
        check(handMask >= 0) { "native renderer cannot draw \"$sceneId\"" }
        viz.setLook(spec.look.copy(sceneId = sceneId))
        // A track it cannot hear fails the render; it used to export a picture the silence rule froze (#999 F1).
        check(GeodeNative.exportJobWarm(job)) { "the track's audio could not be heard" }
    }

    /** Draws frame [frame] of the range into the current target; the caller presents it. */
    fun renderFrame(frame: Int) {
        check(GeodeNative.exportJobFrame(job, frame.toLong(), 0)) { "the track's audio could not be decoded" }
    }

    /** Frees the job, then the renderer it drives. Safe to call more than once. */
    fun release() {
        GeodeNative.exportJobDestroy(job)
        job = 0L
        bestEffort(TAG, "viz.destroy()") { viz.destroy() }
    }

    // What MediaExtractor and the AIFF reader were each handed for a content URI: the declared span, or the whole
    // descriptor when none is declared. Detached: native owns the descriptor and closes it.
    private fun openTrack(): Triple<Int, Long, Long> {
        val afd = requireNotNull(context.contentResolver.openAssetFileDescriptor(spec.audioUri, "r")) { "the track cannot be opened" }
        val length = afd.declaredLength
        val offset = if (length == AssetFileDescriptor.UNKNOWN_LENGTH) 0L else afd.startOffset
        return Triple(afd.parcelFileDescriptor.detachFd(), offset, length)
    }

    private companion object {
        const val TAG = "ExportScene"
    }
}
