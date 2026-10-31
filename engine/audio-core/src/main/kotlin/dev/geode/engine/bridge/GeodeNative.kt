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

    /**
     * Reads the tags of [fd], which native closes. Fills [texts] with UTF-8 title, artist, album, album artist,
     * genre, comment; [ints] with year, track, duration ms, art bytes; [gains] with track gain dB, track peak,
     * album gain dB, album peak. Returns the mask of gains present (1, 2, 4, 8), -1 when unreadable.
     */
    external fun tagsRead(
        fd: Int,
        texts: Array<ByteArray?>,
        ints: IntArray,
        gains: FloatArray,
    ): Int

    /**
     * Writes the six UTF-8 [texts] plus [year] and [track] to [fd], which native closes. A missing array
     * slot leaves that field unchanged. On a false return, see [tagsLastError] for why.
     */
    external fun tagsWrite(
        fd: Int,
        texts: Array<ByteArray>,
        year: Int,
        track: Int,
    ): Boolean

    external fun playerCreate(): Long

    external fun playerDestroy(handle: Long)

    /** [fd] belongs to native from here on; [length] <= 0 reads to the end. */
    external fun playerOpen(
        handle: Long,
        fd: Int,
        offset: Long,
        length: Long,
        token: Long,
    )

    /** Pre-rolls the track that follows for a gapless join; [fd] < 0 clears it. */
    external fun playerSetNext(
        handle: Long,
        fd: Int,
        offset: Long,
        length: Long,
        token: Long,
    )

    external fun playerPlay(handle: Long)

    external fun playerPause(handle: Long)

    external fun playerStop(handle: Long)

    external fun playerSeek(
        handle: Long,
        positionUs: Long,
    )

    external fun playerSetCrossfade(
        handle: Long,
        durationMs: Int,
        curve: Int,
    )

    /** [dsp] is a dspCreate handle at the player's output rate with 2 channels, or 0 to bypass. */
    external fun playerSetDsp(
        handle: Long,
        dsp: Long,
    )

    external fun playerSetVolume(
        handle: Long,
        volume: Float,
    )

    external fun playerState(handle: Long): Int

    external fun playerPlayWhenReady(handle: Long): Boolean

    external fun playerPositionUs(handle: Long): Long

    external fun playerDurationUs(handle: Long): Long

    external fun playerCurrentToken(handle: Long): Long

    external fun playerOutputSampleRate(handle: Long): Int

    external fun playerLastError(handle: Long): String

    /** Copies up to [frames] stereo float frames of the mix into the direct [buffer]; returns the frames copied. */
    external fun playerReadTap(
        handle: Long,
        buffer: java.nio.ByteBuffer,
        frames: Int,
    ): Int

    /** [assets] is the app's `android.content.res.AssetManager`; typed as Any because this module has no Android SDK. */
    external fun vizCreate(
        assets: Any,
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

    external fun vizWriteAudio(
        handle: Long,
        interleaved: FloatArray,
        frames: Int,
        channels: Int,
        sampleRate: Int,
    )

    external fun vizResetAudio(handle: Long)

    external fun vizSoundFrame(
        handle: Long,
        out: FloatArray,
    )

    /** The reaction source catalogue (core/viz/Reactions.cpp), see `dev.geode.render.ReactSourceCatalog`. */
    external fun reactSourceCatalog(): String

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

    /** Every reaction route, `REACT_ROUTE_FLOATS` per route (geode_viz_set_reactions). */
    external fun vizSetReactions(
        handle: Long,
        routes: FloatArray,
    )

    /**
     * A style's reaction DNA (core/viz/ReactionDna), `REACT_ROUTE_FLOATS` per route, in the
     * [vizSetReactions] layout. Needs no viz handle: the same id and seed always give the same routes.
     */
    external fun styleReactions(
        styleId: String,
        seed: Int,
    ): FloatArray

    /** Gyro look, -1..1 per axis; read only while camGyro is on. */
    external fun vizSetLook(
        handle: Long,
        yaw: Float,
        pitch: Float,
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

    external fun vizSetQuality(
        handle: Long,
        renderScale: Float,
        marchScale: Float,
        postFx: Boolean,
        adaptive: Boolean,
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

    /** Not RT-safe on the native side: only call this on a dsp handle not yet passed to [playerSetDsp]. */
    external fun dspSetSampleRate(
        handle: Long,
        sampleRate: Int,
    )

    external fun dspSampleRate(handle: Long): Int

    /** The reason the most recent [tagsWrite] on this thread returned false; GEODE_TAGS_OK (0) otherwise. */
    external fun tagsLastError(): Int

    /**
     * Full-frame RGBA8 overlay drawn last, premultiplied alpha, over the finished composite. `pixels`
     * is Android's `Bitmap.getPixels` ARGB layout, width*height entries; null (or a non-positive
     * size) clears it. Any thread; latched for the next frame.
     */
    external fun vizSetOverlay(
        handle: Long,
        pixels: IntArray?,
        width: Int,
        height: Int,
    )

    /**
     * Full-frame RGBA8 underlay blended UNDER/INTO the scene: [blend] 0 = screen (replaces the scene
     * where it is black), 1 = multiply, 2 = add; [amount] 0..1. Same pixel layout as [vizSetOverlay];
     * null (or a non-positive size) clears it. Any thread; latched for the next frame.
     */
    external fun vizSetUnderlay(
        handle: Long,
        pixels: IntArray?,
        width: Int,
        height: Int,
        blend: Int,
        amount: Float,
    )

    /**
     * The scene and style catalogue (core/viz/Catalogue) as its versioned line table: style ids and
     * kinds, the style families, which controls reach which styles, the control ranges. Static; parse
     * once (dev.geode.render.scene.Catalogue does).
     */
    external fun catalogueText(): String

    /** Every rolled control as `TAB|ParamKeys string` lines, in roll order (core/viz/ParamRandomizer). Static. */
    external fun paramsRollKeys(): String

    /**
     * Rolls a new look (core/viz/ParamRandomizer) from [input], a SceneParamsCodec frame, into [output]:
     * the controls of [tab] (a CustomizeTab name; null = every tab) that reach [styleId] (null = any),
     * except the newline-joined ParamKeys in [locked]. Same arguments, same look. True when it changed
     * something.
     */
    external fun paramsRandomize(
        styleId: String?,
        tab: String?,
        locked: String?,
        seed: Long,
        input: FloatArray,
        output: FloatArray,
    ): Boolean

    /** [input] with exactly the fields [tab] (a CustomizeTab name) rolls put back to their defaults, into [output]. */
    external fun paramsResetTab(
        tab: String,
        input: FloatArray,
        output: FloatArray,
    )

    /** The A/B blend of two SceneParamsCodec frames: floats glide, choices snap at [t] = 0.5. */
    external fun paramsBlend(
        a: FloatArray,
        b: FloatArray,
        t: Float,
        output: FloatArray,
    )

    // Export resampler (core/audio/dsp/PcmResampler): interleaved 16-bit PCM to the AAC rate.

    /** 0 for a bad rate or channel count. */
    external fun resamplerCreate(
        inRate: Int,
        outRate: Int,
        channels: Int,
    ): Long

    external fun resamplerDestroy(handle: Long)

    /**
     * Converts [frames] interleaved frames of [input] (at the handle's channel count); returns the
     * interleaved output, state carrying over. Empty when [input] holds fewer than [frames] frames.
     */
    external fun resamplerProcess(
        handle: Long,
        input: ShortArray,
        frames: Int,
    ): ShortArray

    // Export loudness meter (core/audio/dsp/Loudness): BS.1770-4 integrated loudness and true peak.

    external fun loudnessSupports(channels: Int): Boolean

    /** 0 for a bad rate or a channel count [loudnessSupports] refuses. */
    external fun loudnessCreate(
        sampleRate: Int,
        channels: Int,
    ): Long

    external fun loudnessDestroy(handle: Long)

    /** Consumes [count] interleaved samples (not frames) of [samples], each in -1..1. */
    external fun loudnessFeed(
        handle: Long,
        samples: FloatArray,
        count: Int,
    )

    external fun loudnessHasBlock(handle: Long): Boolean

    /**
     * Fills [out] (5 doubles: integrated LUFS, true peak dBTP, sample peak dBFS, measured ms, gated
     * blocks); returns false when nothing cleared the -70 LUFS gate, in which case out[0] is meaningless.
     */
    external fun loudnessFinish(
        handle: Long,
        out: DoubleArray,
    ): Boolean
}
