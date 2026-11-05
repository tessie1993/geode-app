package dev.geode.engine.bridge

object GeodeNative {
    init {
        System.loadLibrary("geode")
    }

    /** One playback DSP for a player's lifetime; its chains follow the formats [dspConfigure] stages. */
    external fun dspCreate(): Long

    /** Stages the chain for the format queued after the next [dspFlush]; false when it cannot run it. */
    external fun dspConfigure(
        handle: Long,
        sampleRate: Int,
        channels: Int,
    ): Boolean

    /** Makes the staged chain live, then clears its filter state. */
    external fun dspFlush(handle: Long)

    external fun dspDestroy(handle: Long)

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

    /**
     * [buffer] must be a direct float buffer holding [frames] interleaved frames of [channels] channels, the
     * format they were queued in; processed in place, never past the buffer's capacity.
     */
    external fun dspProcess(
        handle: Long,
        buffer: java.nio.ByteBuffer,
        frames: Int,
        channels: Int,
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
     * slot leaves that field unchanged.
     */
    external fun tagsWrite(
        fd: Int,
        texts: Array<ByteArray>,
        year: Int,
        track: Int,
    ): Boolean

    /**
     * Voice "play X" over the library (core/library/Search). [rows] holds three UTF-8 strings per track
     * (title, artist, album) in library order; [request] the query, then MediaStore's focus, artist, album
     * and title extras (null = absent). Writes matching track indices into [out] and returns how many
     * match in all; nothing asked matches every track, 0 means no match.
     */
    external fun librarySearch(
        rows: Array<ByteArray?>,
        request: Array<ByteArray?>,
        out: IntArray,
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

    external fun vizTakeDirectorStyleMoments(handle: Long): Int

    external fun vizWriteAudio(
        handle: Long,
        interleaved: FloatArray,
        frames: Int,
        channels: Int,
        sampleRate: Int,
    )

    external fun vizResetAudio(handle: Long)

    external fun vizSetSoundTuning(
        handle: Long,
        tuning: FloatArray,
    )

    /** The reaction source catalogue (core/viz/Reactions.cpp), see `dev.geode.render.ReactSourceCatalog`. */
    external fun reactSourceCatalog(): String

    /** Customize › Macros (core/viz/Macros): the six knobs, -1..1, for every renderer in the process. */
    external fun macrosSet(knobs: FloatArray)

    /**
     * The auto-director (core/viz/Director): the music's feeling decides palette, behaviour and routing over
     * every style. [amount] 0 off .. 1 full; [seed] is the play's, so a new seed starts the song afresh and an
     * export given the play's seed makes the same film.
     */
    external fun directorSet(
        amount: Float,
        seed: Long,
    )

    /**
     * Note & key reactions (core/viz/NoteResponse): the notes, chords, key and mood drive the style knobs,
     * the note colours, the tint toward them and a gentle shape and distortion. Process-wide like
     * the director, so the screen, an export, the wallpaper and the dream agree; off is exactly the picture
     * without it. The play's seed is the director's ([directorSet]), so an export answers the notes the same way.
     */
    external fun noteResponseSet(enabled: Boolean)

    /** Surprise: unlocked knobs (bit i of [lockMask] locks knob i) to new spots, seeded. */
    external fun macrosSurprise(
        knobs: FloatArray,
        lockMask: Int,
        seed: Long,
    ): FloatArray

    /** The built-in macro presets, "Name|e,s,c,m,sp,ch" lines. */
    external fun macroStarters(): String

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

    /**
     * Customize › Music's hand mask (core/viz Reactions.hpp `hand::`): bit i is moment i set by hand,
     * [HAND_UNCLAIMED] every source no moment claims, [HAND_ALL] a whole list of the user's. Process-wide like
     * [directorSet]: every renderer's director keeps its hands off those moments.
     */
    external fun handSet(mask: Int)

    /**
     * What plays (geode_style_reactions_hand): the [user]'s routes on the [mask]'s sources, the style's DNA on
     * every other, in the [vizSetReactions] layout. [user] null is "all Auto", the DNA alone.
     */
    external fun styleReactionsHand(
        styleId: String,
        seed: Int,
        user: FloatArray?,
        mask: Int,
    ): FloatArray

    /** Customize › Music's rows, [MUSIC_STATE_FLOATS] per moment (geode_music_state). */
    external fun musicState(
        styleId: String,
        seed: Int,
        user: FloatArray?,
        mask: Int,
    ): FloatArray

    /**
     * One tap on Customize › Music (geode_music_edit: [op] 0 toggle [action], 1 strength [value], 2 reset to
     * Auto). Returns the user's new routes; writes the new mask to `outHand[0]` and 1 to `outHand[1]` while the
     * user still has a list (0: all Auto).
     */
    external fun musicEdit(
        styleId: String,
        seed: Int,
        user: FloatArray?,
        mask: Int,
        op: Int,
        moment: Int,
        action: Int,
        value: Float,
        outHand: IntArray,
    ): FloatArray

    /** The LfoTarget ordinal a new route of Music [action] moves (geode_music_action_target). */
    external fun musicActionTarget(action: Int): Int

    /** Mirrors GEODE_HAND_UNCLAIMED. */
    const val HAND_UNCLAIMED: Int = 0x40000000

    /** Mirrors GEODE_HAND_ALL: every moment and every unclaimed source, the whole list the user's. */
    const val HAND_ALL: Int = 0x400003FF

    /** Mirrors GEODE_MUSIC_STATE_FLOATS: mine, route count, strength, on-action bits, fits-action bits. */
    const val MUSIC_STATE_FLOATS: Int = 5

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

    /** Settings › Performance › Liquid on a hot phone: 0 simplify, 1 full, 2 pause (`liquid::HotPolicy`). */
    external fun vizSetLiquidHotPolicy(
        handle: Long,
        policy: Int,
    )

    /** What Liquid mode ran last frame: 0 off, 1 full, 2 lite, 3 warp, 4 paused (`liquid::Mode`). */
    external fun vizLiquidMode(handle: Long): Int

    /**
     * Pins this renderer's auto-director (an export's): from then on it ignores [directorSet], so the live
     * player moving to another track or past the free taste never reaches a render in progress.
     */
    external fun vizSetDirector(
        handle: Long,
        amount: Float,
        seed: Long,
    )

    external fun vizSetOffscreen(
        handle: Long,
        on: Boolean,
    )

    /** Export: step every frame by exactly 1/[fps] instead of measuring frame times; 0 = live. */
    external fun vizSetFrameRate(
        handle: Long,
        fps: Int,
    )

    /**
     * Frosted glass under the app's sheet (`core/viz/UiBlurPass`): the renderer blurs its finished frame
     * inside one rounded rect. The rect is normalised to the viewport, y down (view pixels over the
     * visual's size); [strength] 0..1, 0 = off and free; [cornerPx] in surface pixels. Ignored offscreen.
     */
    external fun vizSetUiBlurRegion(
        handle: Long,
        left: Float,
        top: Float,
        right: Float,
        bottom: Float,
        strength: Float,
        cornerPx: Float,
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

    /**
     * Export only (Studio 2D): the styles this segment switches to, as timed cues ([atMs] render ms
     * since the segment began, [sceneIds], [milkPaths] with "" for none, all the same length) and a
     * beat cut through [cutIds] every [everyBeats] beats. The native renderer decides when
     * (`core/viz/StyleSchedule`); empty arrays clear it. [lockedIds] is the entitlement: styles this
     * user may not draw, dropped from the schedule natively (empty for Pro).
     */
    external fun vizSetStyleSchedule(
        handle: Long,
        atMs: DoubleArray,
        sceneIds: Array<String>,
        milkPaths: Array<String>,
        cutIds: Array<String>,
        everyBeats: Int,
        lockedIds: Array<String>,
    )

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

    /**
     * Full-frame RGBA8 underlay blended UNDER/INTO the scene: [blend] 0 = screen (replaces the scene
     * where it is black), 1 = multiply, 2 = add; [amount] 0..1. `pixels` is Android's `Bitmap.getPixels`
     * ARGB layout, width*height entries; null (or a non-positive size) clears it. Any thread; latched
     * for the next frame.
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

    // Loudness targets (core/audio/dsp/LoudnessTarget): the windows, the gain and ceiling decision and which
    // advice applies. The sentences stay Kotlin (export/LoudnessTarget.kt), keyed by the kind.

    const val LOUDNESS_TARGET_YOUTUBE = 0
    const val LOUDNESS_TARGET_SHORTS = 1
    const val LOUDNESS_TARGET_AS_IS = 2

    const val ADVICE_NOTHING_TO_MEASURE = 0
    const val ADVICE_AS_MIXED = 1
    const val ADVICE_ON_TARGET = 2
    const val ADVICE_UNCHANGED = 3
    const val ADVICE_RAISE = 4
    const val ADVICE_RAISE_LIMITED = 5
    const val ADVICE_LOWER_CLIPPING = 6
    const val ADVICE_LOWER_LOUD_LIMITED = 7
    const val ADVICE_LOWER_LOUD = 8
    const val ADVICE_LOWER_PEAK = 9

    /** Indices into the double[] [loudnessAdvise] fills. */
    const val ADVICE_GAIN_DB = 0
    const val ADVICE_GAIN_FOR_LEVEL_DB = 1
    const val ADVICE_LUFS = 2
    const val ADVICE_TRUE_PEAK_DBTP = 3
    const val ADVICE_RESULTING_LUFS = 4
    const val ADVICE_RESULTING_TRUE_PEAK_DBTP = 5
    const val ADVICE_CEILING_DBTP = 6
    const val ADVICE_QUIET_UNDER_YOUTUBE_LU = 7
    const val ADVICE_PEAK_WARNING = 8
    const val ADVICE_DOUBLES = 9

    /**
     * What to do about a measured file for [target] (LOUDNESS_TARGET_*): returns the ADVICE_* kind and
     * fills [out] ([ADVICE_DOUBLES], ADVICE_* indices; the peak warning is 1.0 or 0.0).
     */
    external fun loudnessAdvise(
        hasIntegrated: Boolean,
        lufs: Double,
        truePeakDbtp: Double,
        target: Int,
        out: DoubleArray,
    ): Int

    // Export planning and clip maths (core/output, core/signal/timeline, viz/ClipGrade, util/CubeLut,
    // audio/dsp/PcmConvert). Pure functions, any thread; the Kotlin exporters only marshal through these.

    /** Frames a render of [durationUs] draws at [fps], at least 1. */
    external fun exportTotalFrames(
        durationUs: Long,
        fps: Int,
    ): Long

    /** Encoder timestamp of [frame]: floor(frame * 1e9 / fps), exact over any length. */
    external fun exportFramePtsNs(
        frame: Long,
        fps: Int,
    ): Long

    /** Programme time [frame] shows, floor(frame * 1000 / fps) ms. */
    external fun exportFrameTimeMs(
        frame: Long,
        fps: Int,
    ): Long

    /** Fields of the long[7] [loopSpecOf] and [loopSpecBudgeted] fill. */
    const val LOOP_SPEC_LOOP_MS = 0
    const val LOOP_SPEC_CROSSFADE_MS = 1
    const val LOOP_SPEC_FPS = 2
    const val LOOP_SPEC_LOOP_FRAMES = 3
    const val LOOP_SPEC_CROSSFADE_FRAMES = 4
    const val LOOP_SPEC_SOURCE_FRAMES = 5
    const val LOOP_SPEC_LOOP_DURATION_US = 6
    const val LOOP_SPEC_LONGS = 7

    /** Loose UI numbers clamped into a valid loop spec, written to [out] (LOOP_SPEC_* order). */
    external fun loopSpecOf(
        loopMs: Long,
        crossfadeMs: Long,
        fps: Int,
        out: LongArray,
    )

    /** The derived fields of the spec as given, no clamping, into [out] (LOOP_SPEC_* order). */
    external fun loopSpecDerive(
        loopMs: Long,
        crossfadeMs: Long,
        fps: Int,
        out: LongArray,
    )

    /** The spec with its dissolve shortened to what the seam stash holds at [width] x [height]. */
    external fun loopSpecBudgeted(
        loopMs: Long,
        crossfadeMs: Long,
        fps: Int,
        width: Int,
        height: Int,
        out: LongArray,
    )

    external fun loopSeamWeight(
        tail: Int,
        seamFrames: Int,
    ): Float

    /** Every repeat of a long-form loop as (offset µs, loop index) pairs. */
    external fun loopExtendPlan(
        loopUs: Long,
        totalUs: Long,
        loops: Int,
    ): LongArray

    /** What the long file weighs: every repeat copies [loopBytes] again, plus 2 % for the sample tables. */
    external fun loopEstimatedBytes(
        loopUs: Long,
        totalUs: Long,
        loopBytes: Long,
        soundtrackBytes: Long,
    ): Long

    /** The video bit rate that keeps a file [totalUs] long under [limitBytes]. */
    external fun loopFittingBitRate(
        totalUs: Long,
        limitBytes: Long,
        soundtrackBytes: Long,
    ): Int

    /** [requestedBps], lowered so the file fits [limitBytes] (<= 0: the 3.8 GB render target). */
    external fun exportCappedVideoBps(
        requestedBps: Int,
        durationUs: Long,
        audioBytes: Long,
        limitBytes: Long,
    ): Int

    external fun exportAudioBytes(
        durationUs: Long,
        bps: Int,
    ): Long

    /** Where a long-form copy stops writing: 4 GiB less 64 MiB for the index. */
    external fun exportMuxerHardLimitBytes(): Long

    /** The encoded frame for a short side and a ratio, into out[0] (width) and out[1] (height). */
    external fun exportFrameSize(
        shortSide: Int,
        wRatio: Int,
        hRatio: Int,
        out: IntArray,
    )

    external fun exportFallbackFps(requestedFps: Int): Int

    /**
     * A part of the track to render, both ends clamped into it: true with [out] = (start ms, duration ms);
     * false for the whole track (no length, a part under [exportRangeMinMs], or the whole of it).
     */
    external fun exportRangeClamp(
        startMs: Long,
        endMs: Long,
        trackMs: Long,
        out: LongArray,
    ): Boolean

    external fun exportRangeMinMs(): Long

    /** Per clip, [renderPlan]'s spans hold start ms, end ms, source in ms and source duration ms (0 = unbounded). */
    const val PLAN_CLIP_LONGS: Int = 4

    /** Per clip, [renderPlan]'s lanes hold the lane's index in the timeline, then the PLAN_* flags. */
    const val PLAN_CLIP_INTS: Int = 2
    const val PLAN_ENABLED: Int = 1

    /** A scene on a visual lane, a song on an audio lane. */
    const val PLAN_WANTED: Int = 2

    /** The clip's lane is not muted or hidden. */
    const val PLAN_LANE_LIVE: Int = 4

    /** Size of [renderPlan]'s out: visual index, song index, programme start, duration, song start, whole song (1/0). */
    const val RENDER_PLAN_LONGS: Int = 6

    /**
     * One visual over one song (core/output/RenderPlan): the first enabled scene on the first live visual lane over
     * the earliest enabled song it overlaps on the first live audio lane. True with [out] filled; false for no visual,
     * no song under it, or less than [minDurationMs] of the two together.
     */
    external fun renderPlan(
        visualSpans: LongArray,
        visualLanes: IntArray,
        audioSpans: LongArray,
        audioLanes: IntArray,
        minDurationMs: Long,
        out: LongArray,
    ): Boolean

    /**
     * Size of the render-ETA state [renderEtaSample] carries: steady, based, shown (1.0 or 0.0), base
     * progress, latest progress, shown seconds, steady-since ms, base ms, shown-at ms. All zeros is a fresh
     * estimate, so starting over is `fill(0.0)`.
     */
    const val ETA_STATE_DOUBLES = 9

    /** Seconds left for the running render, or -1 while there is not yet enough steady progress to say. */
    external fun renderEtaSample(
        state: DoubleArray,
        progress: Float,
        atMs: Long,
        steady: Boolean,
    ): Long

    const val ETA_ALMOST_DONE = 0
    const val ETA_ABOUT_A_MINUTE = 1
    const val ETA_MINUTES = 2
    const val ETA_HOURS = 3

    /** Which ETA_* phrase [secondsLeft] reads as; [outCount] gets the rounded minutes or hours. */
    external fun renderEtaPhrase(
        secondsLeft: Long,
        outCount: LongArray,
    ): Int

    /**
     * The caption cues touching [spanStartMs]..[spanEndMs], re-based to the item, as (start ms, end ms, source
     * index) triples; empty when none does. Nothing native is held: the caller keeps the array.
     */
    external fun cuesForSpan(
        startsMs: LongArray,
        endsMs: LongArray,
        spanStartMs: Long,
        spanEndMs: Long,
    ): LongArray

    /** The item's caption clock for [cuesAt]: (has origin, origin us); all zeros before its first frame. */
    const val CUE_ORIGIN_LONGS = 2

    /**
     * The source index of the caption at a frame's time, -1 for none. [origin] is the item's clock
     * ([CUE_ORIGIN_LONGS] longs, zeros at first); the first frame fixes it in place.
     */
    external fun cuesAt(
        triples: LongArray,
        origin: LongArray,
        presentationUs: Long,
    ): Int

    /** Chapter spans as (start ms, duration ms, source) triples; empty under two clips. */
    external fun chaptersFromClips(
        startsMs: LongArray,
        durationsMs: LongArray,
    ): LongArray

    /** Chapter spans from markers as triples; source -1 is the lead-in that makes the list start at 0:00. */
    external fun chaptersFromMarkers(
        markersMs: LongArray,
        totalMs: Long,
    ): LongArray

    /**
     * A keyframed clip speed as Media3 segments. Keys are parallel arrays: [keyTimesMs], [keyKinds]
     * (0 scalar, 1 vector2, 2 colour, 3 toggle, 4 choice), [keyInterps] (0 hold, 1 linear, 2 bezier),
     * four [keyValues] and four [keyCurves] floats per key. Returns [source span µs, segment starts µs…]
     * with the speeds written to [speedsOut]; null when there are no keys.
     */
    external fun speedRampBuild(
        keyTimesMs: LongArray,
        keyKinds: IntArray,
        keyValues: FloatArray,
        keyInterps: IntArray,
        keyCurves: FloatArray,
        clipStartMs: Long,
        clipDurationMs: Long,
        stepMs: Long,
        speedsOut: FloatArray,
    ): LongArray?

    /** A saved clip speed made safe: NaN, infinite, 0 or negative is 1x, else 0.25..4. */
    external fun clipSpeedSanitize(speed: Float): Float

    /** The clip grade as a column-major 4x4 into [out] (16 floats). */
    external fun clipGradeMatrix(
        brightness: Float,
        contrast: Float,
        saturationPercent: Float,
        hueDegrees: Float,
        out: FloatArray,
    )

    /** A .cube file's ARGB entries, size^3 in [r][g][b] order; null when it is not a usable cube. */
    external fun cubeLutParse(text: ByteArray): IntArray?

    external fun cubeLutGamma(
        red: Float,
        green: Float,
        blue: Float,
        size: Int,
    ): IntArray

    /** Interleaved s16 [frames] from [srcChannels] down to [dstChannels] (1 or 2); empty when not a downmix. */
    external fun pcmDownmixS16(
        input: ShortArray,
        frames: Int,
        srcChannels: Int,
        dstChannels: Int,
    ): ShortArray

    /** Float PCM to s16: clamp, x32767, truncate; NaN is 0. */
    external fun pcmFloatToS16(
        input: FloatArray,
        count: Int,
    ): ShortArray

    /** s16 to float by 1/32768 into [out]. */
    external fun pcmS16ToFloat(
        input: ShortArray,
        count: Int,
        out: FloatArray,
    )

    // Rest when silent (core/sound/Silence): the wallpaper's and the dream's silence rule.

    /**
     * The time sound was last heard, given this poll's newest [frames] samples per channel (0 when
     * nothing new was written): [nowMs] when any is above about -42 dBFS, else [heardAtMs].
     */
    external fun silenceHeardAt(
        left: FloatArray,
        right: FloatArray,
        frames: Int,
        nowMs: Long,
        heardAtMs: Long,
    ): Long

    /** Whether a surface may rest: nothing heard for 6 s. */
    external fun silenceResting(
        nowMs: Long,
        heardAtMs: Long,
    ): Boolean
}
