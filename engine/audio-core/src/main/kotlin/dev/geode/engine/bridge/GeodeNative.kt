package dev.geode.engine.bridge

object GeodeNative {
    init {
        System.loadLibrary("geode")
    }

    external fun dspCreate(
        sampleRate: Int,
        channels: Int,
    ): Long

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
     * slot leaves that field unchanged.
     */
    external fun tagsWrite(
        fd: Int,
        texts: Array<ByteArray>,
        year: Int,
        track: Int,
    ): Boolean

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
     * the note colours, the blooms over every style and a gentle shape and distortion. Process-wide like
     * the director, so the screen, an export, the wallpaper and the dream agree; off is exactly the picture
     * without it. The play's seed is the director's ([directorSet]), so an export draws the same blooms.
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

    // Studio preview clock (core/studio/PreviewClock): the live preview's programme time. One owner
    // thread per handle; the app wraps it in `ui/StudioPreviewClock`.

    external fun previewCreate(): Long

    external fun previewDestroy(handle: Long)

    external fun previewSetSpans(
        handle: Long,
        startsMs: LongArray,
        durationsMs: LongArray,
    )

    external fun previewSetRate(
        handle: Long,
        fps: Int,
        lengthMs: Long,
    )

    external fun previewPlay(
        handle: Long,
        programmeMs: Long,
        nowNs: Long,
    )

    external fun previewPause(
        handle: Long,
        nowNs: Long,
    )

    external fun previewSeek(
        handle: Long,
        programmeMs: Long,
        nowNs: Long,
    )

    external fun previewAudio(
        handle: Long,
        span: Int,
        spanPosMs: Long,
        nowNs: Long,
    )

    /** Programme time at [nowNs], on a frame boundary. */
    external fun previewNow(
        handle: Long,
        nowNs: Long,
    ): Long

    external fun previewEnded(
        handle: Long,
        nowNs: Long,
    ): Boolean

    /** The audio span under [programmeMs] (its offset into [offsetOut]`[0]`), or -1 in a gap. */
    external fun previewLocate(
        handle: Long,
        programmeMs: Long,
        offsetOut: LongArray,
    ): Int

    /** The start of the frame [programmeMs] falls on at [fps]. */
    external fun previewFrameOf(
        programmeMs: Long,
        fps: Int,
    ): Long
}
