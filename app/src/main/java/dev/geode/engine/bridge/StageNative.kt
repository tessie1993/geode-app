package dev.geode.engine.bridge

// The JNI declarations of geode_stage_jni.cpp (CLAUDE.md).
object StageNative {
    init {
        GeodeNative.ensureLoaded()
    }

    /** Mirrors GEODE_FRAME_LIQUID_CHANGED: Liquid mode changed; read it with [stageLiquidMode]. */
    const val FRAME_LIQUID_CHANGED: Int = 0x4

    /** Mirrors GEODE_FRAME_ERROR_CHANGED: the error text changed; read it with [VizNative.vizLastError]. */
    const val FRAME_ERROR_CHANGED: Int = 0x8

    /** Mirrors GEODE_FRAME_MILK_LOADED: MilkDrop finished loading a preset; take it with [VizNative.vizTakeMilkPresetLoaded]. */
    const val FRAME_MILK_LOADED: Int = 0x10

    /** Mirrors GEODE_FRAME_DIRECTOR_MOMENT: take the moments with [VizNative.vizTakeDirectorStyleMoments]. */
    const val FRAME_DIRECTOR_MOMENT: Int = 0x20

    /** Mirrors GEODE_FRAME_STILL_READY: the still asked for is ready ([stageStillSize], [stageStillRead]). */
    const val FRAME_STILL_READY: Int = 0x40

    // The native stage (core/stage, core/api/geode_stage_api.h): one surface's render thread, EGL context, vsync
    // and renderer. A handle is an id that is never reused: every call on one that is gone does nothing and
    // answers 0, from any thread. Native never calls up; the shell polls [stageTakeEvents].

    /** [assets] is the AssetManager (Any, as for [VizNative.vizCreate]); [kind] a STAGE_KIND_*; [capFps] the surface's cap (<= 0 none). 0 when it failed. */
    external fun stageCreate(
        assets: Any,
        cacheDir: String,
        milkTextureDir: String,
        kind: Int,
        capFps: Float,
    ): Long

    /** Quits the stage; its thread frees the renderer. Waits at most 200 ms. */
    external fun stageDestroy(stage: Long)

    /** The `android.view.Surface` from surfaceCreated, or null from surfaceDestroyed; returns once the stage let go of the old one (bounded). */
    external fun stageSetWindow(
        stage: Long,
        surface: Any?,
        width: Int,
        height: Int,
    )

    external fun stageSetSize(
        stage: Long,
        width: Int,
        height: Int,
    )

    /** What the app's view sees of itself: the STAGE_VIEW_* bits. The stage moves while all four hold. */
    external fun stageSetView(
        stage: Long,
        viewFlags: Int,
    )

    /** The wallpaper and the dream: the SURFACE_* bits through the surface run rule; answers the run mode applied. */
    external fun stageSetSurface(
        stage: Long,
        surfaceFlags: Int,
        animateUntilMs: Long,
        nowMs: Long,
    ): Int

    external fun stagePause(stage: Long)

    external fun stageResume(stage: Long)

    /** surfaceRedrawNeeded: one frame now, even while the vsync is stopped. */
    external fun stageRedraw(stage: Long)

    external fun stageSetCap(
        stage: Long,
        capFps: Float,
    )

    /** The STAGE_STATE_* bits. */
    external fun stageState(stage: Long): Int

    /** The `FRAME_*` events of the frames since the last call, OR-ed. */
    external fun stageTakeEvents(stage: Long): Int

    /** The ring frames hear (a `pcmRing*` handle), or 0. Returns once no frame reads the ring it replaced. */
    external fun stageSetSound(
        stage: Long,
        ring: Long,
    )

    /**
     * The user's look. Native passes on only the parts that differ from the look set last. [lfos], [adsrs] and
     * [reactions] are the saved texts; [reactions] null means the style's own routes.
     */
    external fun stageSetLook(
        stage: Long,
        sceneId: String,
        params: FloatArray,
        lfos: String,
        adsrs: String,
        tuning: FloatArray?,
        reactions: String?,
        reducedMotion: Boolean,
        transitionId: String,
        transitionMs: Long,
    )

    /** The picture under the style (ARGB ints, [width] x [height]); null clears it. */
    external fun stageSetUnderlay(
        stage: Long,
        pixels: IntArray?,
        width: Int,
        height: Int,
        blend: Int,
        amount: Float,
    )

    external fun stageSetQuality(
        stage: Long,
        renderScale: Float,
        marchScale: Float,
        postFx: Boolean,
    )

    /** Settings › Performance's "Adapt to frame time" (native `viz/frame/FrameGovernor`); every rule is native. */
    external fun stageSetAdaptive(
        stage: Long,
        on: Boolean,
    )

    external fun stageBeginParamMorph(
        stage: Long,
        seconds: Float,
    )

    /** [points] fingers as x, y pairs in NDC; 0 = all lifted. */
    external fun stageSetTouch(
        stage: Long,
        xy: FloatArray,
        points: Int,
    )

    external fun stageSetTouchSmear(
        stage: Long,
        on: Boolean,
        strength: Float,
    )

    external fun stageSetFluidInjection(
        stage: Long,
        force: String?,
        dye: String?,
    )

    external fun stageLoadMilkPreset(
        stage: Long,
        path: String,
    )

    external fun stageReloadMilkPreset(stage: Long)

    /** After [FRAME_MILK_LOADED]: each loaded preset's path once, in order; null when none waits. */
    external fun stageTakeMilkLoaded(stage: Long): String?

    /** After [FRAME_DIRECTOR_MOMENT]: the moments since the last call. */
    external fun stageTakeDirectorMoments(stage: Long): Int

    /** The Liquid mode of the last frame that said [FRAME_LIQUID_CHANGED]. */
    external fun stageLiquidMode(stage: Long): Int

    /** The renderer's error text as of the last [FRAME_ERROR_CHANGED]; "" when it is clean. */
    external fun stageError(stage: Long): String?

    external fun stageGyroPush(
        stage: Long,
        x: Float,
        y: Float,
        z: Float,
        w: Float,
        displayRotation: Int,
    )

    /** Whether the tilt sensor should run now ([wanted] is the user's setting); asked after every view, pause or setting change. */
    external fun stageGyroListens(
        stage: Long,
        wanted: Boolean,
    ): Boolean

    external fun stageSetUiBlurRegion(
        stage: Long,
        left: Float,
        top: Float,
        right: Float,
        bottom: Float,
        strength: Float,
        cornerPx: Float,
    )

    /** The edge glow's floats, or null for none. */
    external fun stageSetEdgeGlow(
        stage: Long,
        opts: FloatArray?,
    )

    external fun stageSetCastSource(
        stage: Long,
        on: Boolean,
    )

    external fun stageKnows(
        stage: Long,
        sceneId: String,
    ): Boolean

    /** The scene ids the renderer has, newline-joined. */
    external fun stageSceneIds(stage: Long): String

    /** Builds a transition's program on the stage thread, once it has a context. */
    external fun stageWarmTransition(
        stage: Long,
        id: String,
    )

    /**
     * Opens Record's tee into [session]'s encoder on the stage thread. [tile] (a direct buffer of premultiplied
     * RGBA8, copied by the call) is the mark, owed when [placement] is not 0. When the tee cannot open, native
     * stops [recorder] as failed. False when the stage is gone: no tee will open or close.
     */
    external fun stageRecordStart(
        stage: Long,
        recorder: Long,
        session: Long,
        tile: java.nio.ByteBuffer?,
        tileWidth: Int,
        tileHeight: Int,
        placement: Int,
        corner: Int,
    ): Boolean

    /** Closes the tee on the stage thread and tells the session; a stage's end does the same. */
    external fun stageRecordStop(stage: Long)

    /** True when no tee is open or waiting to open (always for a stage that is gone): the recorder and session may be freed. */
    external fun stageRecordClosed(stage: Long): Boolean

    /** Asks the next frame for a still; [FRAME_STILL_READY] says when it waits. */
    external fun stageStillRequest(stage: Long)

    /** The waiting still's width and height into [out]; false when none waits. */
    external fun stageStillSize(
        stage: Long,
        out: IntArray,
    ): Boolean

    /** The still's RGBA8 rows, top first, into a direct buffer of at least width * height * 4 bytes; handed out once. */
    external fun stageStillRead(
        stage: Long,
        out: java.nio.ByteBuffer,
    ): Boolean

    external fun stageTapAlong(
        stage: Long,
        uptimeNs: Long,
    ): Boolean

    const val STAGE_KIND_VIEW = 0
    const val STAGE_KIND_SURFACE = 1
    const val STAGE_VIEW_ATTACHED = 1 shl 0
    const val STAGE_VIEW_WINDOW_VISIBLE = 1 shl 1
    const val STAGE_VIEW_SHOWN = 1 shl 2
    const val STAGE_VIEW_OPAQUE = 1 shl 3
    const val STAGE_STATE_SURFACE = 1 shl 2
    const val STAGE_STATE_VSYNC = 1 shl 4
    const val STAGE_STATE_REFUSED = 1 shl 6
}
