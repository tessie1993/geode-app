package dev.geode.engine.bridge

object GeodeNative {
    init {
        System.loadLibrary("geode")
    }

    external fun version(): String

    external fun featureFrameFloats(): Int

    external fun analysisCreate(
        sampleRate: Int,
        fftSize: Int,
        hopRateHz: Float,
    ): Long

    external fun analysisDestroy(handle: Long)

    external fun analysisSetSampleRate(
        handle: Long,
        sampleRate: Int,
    )

    external fun analysisSetTuning(
        handle: Long,
        sensitivity: Float,
        refractoryMs: Float,
        attackSeconds: Float,
        releaseSeconds: Float,
    )

    external fun analysisReset(handle: Long)

    external fun analysisAnalyze(
        handle: Long,
        mid: FloatArray,
        side: FloatArray?,
        dtSeconds: Float,
        out: FloatArray,
    )

    external fun analysisPush(
        handle: Long,
        interleaved: FloatArray,
        frames: Int,
        channels: Int,
    )

    external fun analysisPull(
        handle: Long,
        out: FloatArray,
    ): Boolean

    external fun analysisKey(handle: Long): String

    external fun pulseReplay(
        flux: FloatArray,
        rms: FloatArray,
        hopRateHz: Float,
        sensitivity: Float,
        refractoryMs: Float,
        out: FloatArray,
    )

    external fun drumsCreate(
        bandCount: Int,
        hopRateHz: Float,
        sampleRate: Int,
    ): Long

    external fun drumsDestroy(handle: Long)

    external fun drumsStep(
        handle: Long,
        bands: FloatArray,
        out: FloatArray,
    )

    external fun dspCreate(
        sampleRate: Int,
        channels: Int,
    ): Long

    external fun dspDestroy(handle: Long)

    external fun dspBandCount(): Int

    external fun dspBandCenterHz(band: Int): Float

    external fun dspSetEnabled(
        handle: Long,
        enabled: Boolean,
    )

    external fun dspSetBand(
        handle: Long,
        band: Int,
        millibels: Int,
    )

    external fun dspBand(
        handle: Long,
        band: Int,
    ): Int

    external fun dspSetBassBoost(
        handle: Long,
        permille: Int,
    )

    external fun dspSetLoudnessMb(
        handle: Long,
        millibels: Int,
    )

    external fun dspSetGainDb(
        handle: Long,
        db: Float,
    )

    external fun dspSetCrossfeed(
        handle: Long,
        enabled: Boolean,
    )

    external fun dspSetLimiter(
        handle: Long,
        enabled: Boolean,
    )

    external fun dspReset(handle: Long)

    /** [buffer] must be a direct float buffer holding [frames] interleaved frames; processed in place. */
    external fun dspProcess(
        handle: Long,
        buffer: java.nio.ByteBuffer,
        frames: Int,
    )

    external fun vizCreate(
        assets: android.content.res.AssetManager,
        cacheDir: String,
    ): Long

    external fun vizDestroy(handle: Long)

    external fun vizParamNames(): String

    external fun vizSetParams(
        handle: Long,
        values: FloatArray,
    )

    external fun vizSetParam(
        handle: Long,
        name: String,
        value: Float,
    ): Boolean

    external fun vizSetFeatures(
        handle: Long,
        frame: FloatArray,
    )

    external fun vizSetReducedMotion(
        handle: Long,
        on: Boolean,
    )

    external fun vizSetLayer(
        handle: Long,
        sceneId: String,
        mix: Float,
        blendMode: Int,
    )

    external fun vizSetTransition(
        handle: Long,
        id: String,
        durationMs: Long,
    )

    external fun vizBeginParamMorph(
        handle: Long,
        seconds: Float,
    )

    external fun vizSetTouch(
        handle: Long,
        xy: FloatArray,
        points: Int,
    )

    external fun vizQueueTouchStroke(
        handle: Long,
        nx: Float,
        ny: Float,
        ndx: Float,
        ndy: Float,
        dt: Float,
        strength: Float,
    )

    external fun vizSetFluidInjection(
        handle: Long,
        force: String?,
        dye: String?,
    )

    external fun vizLoadMilkPreset(
        handle: Long,
        path: String,
    )

    external fun vizReloadMilkPreset(handle: Long)

    external fun vizSetMilkTextureDir(
        handle: Long,
        dir: String,
    )

    external fun vizTakeMilkPresetLoaded(handle: Long): String?

    external fun vizPushPcm(
        handle: Long,
        mono: FloatArray,
        count: Int,
    )

    external fun vizSetCustomShader(
        handle: Long,
        sceneId: String,
        fragmentSource: String,
    )

    external fun vizCustomShader(
        handle: Long,
        sceneId: String,
    ): String?

    external fun vizSetLfo(
        handle: Long,
        slot: Int,
        config: FloatArray,
    )

    external fun vizSetAdsr(
        handle: Long,
        slot: Int,
        config: FloatArray,
    )

    external fun vizSetThermal(
        handle: Long,
        platformStatus: Int,
        headroom: Float,
    )

    external fun vizSetPacedFps(
        handle: Long,
        fps: Float,
    )

    external fun vizSetOffscreen(
        handle: Long,
        on: Boolean,
    )

    external fun vizKnows(
        handle: Long,
        sceneId: String,
    ): Boolean

    external fun vizSceneIds(handle: Long): String

    external fun vizLastError(handle: Long): String

    external fun vizSurfaceCreated(handle: Long)

    external fun vizSurfaceChanged(
        handle: Long,
        width: Int,
        height: Int,
    )

    external fun vizSetScene(
        handle: Long,
        sceneId: String,
    ): Boolean

    external fun vizWarmTransition(
        handle: Long,
        id: String,
    )

    external fun vizCut(handle: Long)

    external fun vizRender(
        handle: Long,
        timeSeconds: Double,
        targetFbo: Int,
    )

    external fun vizReleaseScenes(handle: Long)
}
