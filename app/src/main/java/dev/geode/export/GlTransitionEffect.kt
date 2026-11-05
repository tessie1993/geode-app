package dev.geode.export

import android.content.Context
import android.content.res.AssetManager
import androidx.media3.common.VideoFrameProcessingException
import androidx.media3.common.util.GlUtil
import androidx.media3.common.util.Size
import androidx.media3.common.util.UnstableApi
import androidx.media3.effect.BaseGlShaderProgram
import androidx.media3.effect.GlEffect
import androidx.media3.effect.GlShaderProgram
import dev.geode.engine.bridge.GeodeNative
import dev.geode.render.TransitionCatalog
import java.nio.ByteBuffer
import java.nio.ByteOrder

/*
 * A Studio render's clip transitions, as Media3 effects. The GL is native (`core/viz/frame/
 * ExportPasses`: `CapturePass`, `TransitionPass`, #442), with the GLSL in `assets/shaders/
 * export_copy_frag.glsl` and `export_transition_frag.glsl`; these classes are only Media3's shell
 * around it, on Transformer's GL thread, which binds each program's output framebuffer and viewport
 * before it calls drawFrame. The frames are timed by Media3's presentation times, as before.
 */

/** Passes frames through untouched and leaves the last one in [store] when the clip ends. */
@UnstableApi
class TransitionCaptureEffect(
    private val store: TransitionFrameStore,
) : GlEffect {
    override fun toGlShaderProgram(
        context: Context,
        useHdr: Boolean,
    ): GlShaderProgram = TransitionCaptureProgram(store, context.assets, useHdr)
}

/**
 * Opens a clip with a GL Transition from the frame in [store] to the clip's own picture, over
 * [durationUs] from the clip's first frame. Without a captured frame it is a plain pass-through.
 */
@UnstableApi
class GlTransitionEffect(
    private val def: TransitionCatalog.Def,
    private val durationUs: Long,
    private val store: TransitionFrameStore,
) : GlEffect {
    override fun toGlShaderProgram(
        context: Context,
        useHdr: Boolean,
    ): GlShaderProgram = GlTransitionProgram(def.name, durationUs, store, context.assets, useHdr)
}

@UnstableApi
private class TransitionCaptureProgram(
    private val store: TransitionFrameStore,
    assets: AssetManager,
    useHdr: Boolean,
) : BaseGlShaderProgram(useHdr, 1) {
    private val pass = openPass(GeodeNative.exportCaptureCreate(assets))
    private var width = 0
    private var height = 0

    override fun configure(
        inputWidth: Int,
        inputHeight: Int,
    ): Size {
        width = inputWidth
        height = inputHeight
        GeodeNative.exportCaptureConfigure(pass, inputWidth, inputHeight)
        return Size(inputWidth, inputHeight)
    }

    override fun drawFrame(
        inputTexId: Int,
        presentationTimeUs: Long,
    ) {
        GeodeNative.exportCaptureDraw(pass, inputTexId)
        GlUtil.checkGlError()
    }

    override fun release() {
        super.release()
        if (width > 0 && height > 0) {
            val rgba = ByteBuffer.allocateDirect(width * height * BYTES_PER_PIXEL).order(ByteOrder.nativeOrder())
            if (GeodeNative.exportCaptureRead(pass, rgba)) store.frame = TransitionFrameStore.CapturedFrame(width, height, rgba)
        }
        GeodeNative.exportPassDestroy(pass)
    }
}

@UnstableApi
private class GlTransitionProgram(
    id: String,
    durationUs: Long,
    private val store: TransitionFrameStore,
    assets: AssetManager,
    useHdr: Boolean,
) : BaseGlShaderProgram(useHdr, 1) {
    private val pass = openPass(GeodeNative.exportTransitionCreate(assets, id, durationUs))

    override fun configure(
        inputWidth: Int,
        inputHeight: Int,
    ): Size {
        GeodeNative.exportTransitionConfigure(pass, inputWidth, inputHeight)
        store.frame?.let { frame -> GeodeNative.exportTransitionSetFrom(pass, frame.rgba, frame.width, frame.height) }
        return Size(inputWidth, inputHeight)
    }

    override fun drawFrame(
        inputTexId: Int,
        presentationTimeUs: Long,
    ) {
        GeodeNative.exportTransitionDraw(pass, inputTexId, presentationTimeUs)
        GlUtil.checkGlError()
    }

    override fun release() {
        super.release()
        GeodeNative.exportPassDestroy(pass)
    }
}

/** The handle when its pass built; otherwise it is freed and the render fails as a failed program link did. */
private fun openPass(handle: Long): Long {
    val error = if (handle == 0L) "out of memory" else GeodeNative.exportPassError(handle)
    if (error.isEmpty()) return handle
    if (handle != 0L) GeodeNative.exportPassDestroy(handle)
    throw VideoFrameProcessingException("export transition pass failed: $error")
}

private const val BYTES_PER_PIXEL = 4
