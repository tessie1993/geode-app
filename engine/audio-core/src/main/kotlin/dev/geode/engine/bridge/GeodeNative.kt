package dev.geode.engine.bridge

// The one JNI surface (AGENTS.md): every native call is declared here, so its size is the native API's.
@Suppress("TooManyFunctions")
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

    // Playback gain (core/audio/dsp/GainStage): the shell says targets and lengths; the ramps, the dB maths
    // and when each applies are native.

    /** The UI fade (pause, resume, skip) to [db] over [ms]; -inf or <= -80 dB is silence. */
    external fun dspRampTo(
        handle: Long,
        db: Float,
        ms: Int,
    )

    /** The sleep timer's time left (< 0 when cancelled or done); returns the ms until native next needs it. */
    external fun dspSleepFade(
        handle: Long,
        remainingMs: Long,
    ): Long

    external fun dspSetCrossfade(
        handle: Long,
        ms: Int,
    )

    /** Whether something plays after the current item on its own: a next item, or repeat one. */
    external fun dspSetFollowsOn(
        handle: Long,
        followsOn: Boolean,
    )

    external fun dspSetSpeed(
        handle: Long,
        speed: Float,
    )

    /** ReplayGain mode (0 off, 1 track, 2 album), preamp and clip guard. */
    external fun dspSetReplayGain(
        handle: Long,
        mode: Int,
        preampDb: Float,
        clipGuard: Boolean,
    )

    /** One track's ReplayGain tags by stream name (its URI); [mask] holds the 1, 2, 4, 8 bits of those present. */
    external fun dspSetStreamTags(
        handle: Long,
        stream: String,
        mask: Int,
        trackGainDb: Float,
        trackPeak: Float,
        albumGainDb: Float,
        albumPeak: Float,
    )

    /** Playback thread, right after [dspFlush]: the stream whose first sample comes next. */
    external fun dspBeginStream(
        handle: Long,
        stream: String?,
        positionUs: Long,
        durationUs: Long,
    )

    /** Playback thread: the stream's last sample has been queued. */
    external fun dspEndStream(handle: Long)

    external fun dspSetCrossfeed(
        handle: Long,
        enabled: Boolean,
    )

    external fun dspSetLimiter(
        handle: Long,
        enabled: Boolean,
    )

    /**
     * Whether the platform takes the next AudioTrack bit-perfect (#353); from the next [dspFlush] the chain
     * passes every block through untouched until a flush with it off.
     */
    external fun dspSetBitPerfect(
        handle: Long,
        on: Boolean,
    )

    /**
     * [buffer] must be a direct buffer holding [frames] interleaved frames of [channels] channels, the format
     * they were queued in, as 16-bit PCM or as float when [pcmFloat]; processed in place (the int16 conversion
     * is native), never past the buffer's capacity.
     */
    external fun dspProcess(
        handle: Long,
        buffer: java.nio.ByteBuffer,
        frames: Int,
        channels: Int,
        pcmFloat: Boolean,
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

    /**
     * Moves takes saved inside project folders ([projectsDir]`/<id>/takes/`) to [takesDir]`/<id>/`,
     * once (core/library/StudioFiles, #384). Returns how many could not be moved.
     */
    external fun studioMoveTakesOut(
        projectsDir: String,
        takesDir: String,
    ): Int

    /** [assets] is the app's `android.content.res.AssetManager`; typed as Any because this module has no Android SDK. */
    external fun vizCreate(
        assets: Any,
        cacheDir: String,
    ): Long

    external fun vizDestroy(handle: Long)

    external fun vizSetParams(
        handle: Long,
        values: FloatArray,
    )

    external fun vizSetReducedMotion(
        handle: Long,
        on: Boolean,
    )

    /** Process-wide (core/viz/LayerState): every renderer draws it; "" = no layer. */
    external fun layerSet(
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

    /**
     * The live feed: the renderer takes what the ring at [ring] (a [pcmRingCreate] handle; 0 for none)
     * gained since its last call straight into its sound engine, resetting it on a restart. Render thread.
     */
    external fun vizFeedRing(
        handle: Long,
        ring: Long,
    )

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
     * every style. [on] is the user's switch as set, which an export directs by ([vizPinEngineSettings]);
     * [amount] 0 off .. 1 full is what live surfaces drive with now; [seed] is the play's, so a new seed
     * starts the song afresh and an export pinned with it makes the same film.
     */
    external fun directorSet(
        on: Boolean,
        amount: Float,
        seed: Long,
    )

    /**
     * Note & key reactions (core/viz/NoteResponse): the notes, chords, key and mood drive the style knobs,
     * the note colours, the tint toward them and a gentle shape and distortion. Process-wide like
     * the director, so the screen, an export, the wallpaper and the dream agree; off is exactly the picture
     * without it. The play's seed is the director's ([directorSet]). A live renderer mixes fresh entropy into it per play,
     * so the live note response differs every time; an export pins its settings with none
     * ([vizPinEngineSettings]), so exporting the same play twice draws the same picture, though not the one
     * the screen drew.
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

    /** Process-wide (core/viz/ShaderOverrides): every renderer draws it; "" clears it. */
    external fun shaderOverrideSet(
        styleId: String,
        fragmentSource: String,
    )

    /** The style's shader override; null when it draws its built-in shader. */
    external fun shaderOverride(styleId: String): String?

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

    /**
     * The user's reaction [routes] (`REACT_ROUTE_FLOATS` per route; null: none, the DNA plays) under the hand
     * [mask] (geode_viz_set_user_reactions). Native resolves what plays from these, the scene and `reactSeed`.
     */
    external fun vizSetUserReactions(
        handle: Long,
        routes: FloatArray?,
        mask: Int,
    )

    /**
     * A style's reaction DNA (core/viz/ReactionDna), `REACT_ROUTE_FLOATS` per route, in the
     * [vizSetUserReactions] layout. Needs no viz handle: the same id and seed always give the same routes.
     */
    external fun styleReactions(
        styleId: String,
        seed: Int,
    ): FloatArray

    /**
     * Customize › Music's hand mask (core/viz Reactions.hpp `hand::`): bit i is moment i set by hand,
     * [HAND_UNCLAIMED] every source no moment claims, [HAND_ALL] a whole list of the user's. Process-wide like
     * [directorSet]: every renderer's director keeps its hands off those moments. Without a list of the user's
     * ([hasUser] false) native counts the mask as 0, so a stale mask cannot switch off the director's routes.
     */
    external fun handSet(
        mask: Int,
        hasUser: Boolean,
    )

    /**
     * What plays (geode_style_reactions_hand): the [user]'s routes on the [mask]'s sources, the style's DNA on
     * every other, in the [vizSetUserReactions] layout. [user] null is "all Auto", the DNA alone.
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

    /** Bit per Music action whose target reaches [styleId] (geode_music_actions_for). */
    external fun musicActionsFor(styleId: String): Int

    /** The modulation-target table (geode_mod_targets_text), parsed once by `render/ModTable`. */
    external fun modTargetsText(): String

    /** Mirrors GEODE_HAND_UNCLAIMED. */
    const val HAND_UNCLAIMED: Int = 0x40000000

    /** Mirrors GEODE_HAND_ALL: every moment and every unclaimed source, the whole list the user's. */
    const val HAND_ALL: Int = 0x400003FF

    /** Mirrors GEODE_MUSIC_STATE_FLOATS: mine, route count, strength, on-action bits, fits-action bits. */
    const val MUSIC_STATE_FLOATS: Int = 5

    /**
     * Customize › Music's ready-made [recipe]'s routes the style [styleId] can show, in the [vizSetUserReactions]
     * layout; every route it holds for a null [styleId] (geode_event_recipe_routes).
     */
    external fun eventRecipeRoutes(
        recipe: Int,
        styleId: String?,
    ): FloatArray

    /** [RECIPE_ON] and [RECIPE_FITS] for [recipe] on [styleId] over what plays, [current] (geode_event_recipe_state). */
    external fun eventRecipeState(
        recipe: Int,
        styleId: String,
        current: FloatArray,
    ): Int

    /** [current] with [recipe] turned off if on, else on if it fits (geode_event_recipe_toggle). */
    external fun eventRecipeToggle(
        recipe: Int,
        styleId: String,
        current: FloatArray,
    ): FloatArray

    /** Mirrors GEODE_RECIPE_ON: every route the recipe would add to the style already plays. */
    const val RECIPE_ON: Int = 1

    /** Mirrors GEODE_RECIPE_FITS: turning the recipe on stays within GEODE_REACT_ROUTES. */
    const val RECIPE_FITS: Int = 2

    /** One gyro reading (azimuth, pitch in radians from SensorManager.getOrientation); core/viz/GyroLook makes the look. */
    external fun vizGyroSample(
        handle: Long,
        azimuth: Float,
        pitch: Float,
    )

    /** Gyro off or switched on again: recentre the look and recapture the baseline on the next sample. */
    external fun vizGyroReset(handle: Long)

    /**
     * One live frame's start (`core/viz/frame/Cadence`), on the GL thread before [vizSetScene] and
     * [vizRender]: hand over what the shell holds every frame; native applies only what changed. [pacedFps]
     * is this surface's own cap (0 free-running; the thermal cap is applied under it natively); the rest is
     * Settings › Performance, with [liquidHotPolicy] 0 simplify, 1 full, 2 pause (`liquid::HotPolicy`).
     * The four versions are the shell's counters for its params, LFO, ADSR and tuning (`core/viz/frame/
     * PushVersions`): native sets a `FRAME_PUSH_*` bit for each it has not applied, and the shell pushes
     * exactly those before [vizRender]. Answers a bitmask of the `FRAME_*` bits.
     */
    external fun vizFrameBegin(
        handle: Long,
        pacedFps: Float,
        renderScale: Float,
        marchScale: Float,
        postFx: Boolean,
        adaptive: Boolean,
        liquidHotPolicy: Int,
        paramsVersion: Long,
        lfoVersion: Long,
        adsrVersion: Long,
        tuningVersion: Long,
    ): Int

    /** Mirrors GEODE_FRAME_CLEAR_ONLY: native cleared a new surface's first frame; present it, do nothing else. */
    const val FRAME_CLEAR_ONLY: Int = 0x1

    /** Mirrors GEODE_FRAME_THERMAL_DUE: read PowerManager and call [thermalPush]. */
    const val FRAME_THERMAL_DUE: Int = 0x2

    /** Mirrors GEODE_FRAME_LIQUID_CHANGED: Liquid mode changed; read it with [frameLiquidMode]. */
    const val FRAME_LIQUID_CHANGED: Int = 0x4

    /** Mirrors GEODE_FRAME_LIQUID_MODE: 0 off, 1 full, 2 lite, 3 warp, 4 paused (`liquid::Mode`). */
    fun frameLiquidMode(due: Int): Int = (due ushr 8) and 0xF

    /** Mirrors GEODE_FRAME_PUSH_PARAMS: push the scene params ([vizSetParams]). */
    const val FRAME_PUSH_PARAMS: Int = 0x10

    /** Mirrors GEODE_FRAME_PUSH_LFO: push every LFO slot ([vizSetLfo]). */
    const val FRAME_PUSH_LFO: Int = 0x20

    /** Mirrors GEODE_FRAME_PUSH_ADSR: push every ADSR slot ([vizSetAdsr]). */
    const val FRAME_PUSH_ADSR: Int = 0x40

    /** Mirrors GEODE_FRAME_PUSH_TUNING: push the sound tuning ([vizSetSoundTuning]). */
    const val FRAME_PUSH_TUNING: Int = 0x80

    /**
     * Pins an export renderer's engine settings once (geode_viz_pin_engine_settings, #350): from then on it
     * ignores [macrosSet], [noteResponseSet], [handSet] and [directorSet], so live edits never reach a render
     * in progress. The director is decided natively from the user's switch, [entitled] and [durationMs].
     * Returns the pinned hand mask, for the export's route merge.
     */
    external fun vizPinEngineSettings(
        handle: Long,
        entitled: Boolean,
        durationMs: Long,
    ): Int

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

    /**
     * The wallpaper's edge look (`core/viz/frame/EdgeGlowPass`), `GEODE_EDGE_GLOW_FLOATS` floats: enabled
     * (> 0.5), then the on-screen window in surface pixels, y down (left, top, width, height; a size <= 0
     * is the whole surface). Any thread, latched for the next frame; ignored offscreen.
     */
    external fun vizSetEdgeGlow(
        handle: Long,
        opts: FloatArray?,
    )

    /**
     * The live wallpaper's on-screen window (`core/viz/frame/WallpaperWindow.hpp`): from the surface and
     * display sizes and the raw `onOffsetsChanged` values (xOffset, yOffset, xOffsetStep, yOffsetStep,
     * xPixels, yPixels), writes left, top, width, height into [out] (4 floats), as [vizSetEdgeGlow] takes them.
     */
    external fun wallpaperWindow(
        surfaceW: Float,
        surfaceH: Float,
        displayW: Float,
        displayH: Float,
        offsets: FloatArray?,
        out: FloatArray,
    )

    /** The wallpaper's frame rate: [targetFps] lowered to the Performance cap ([capFps] <= 0 = none); never 0. */
    external fun wallpaperFps(
        targetFps: Float,
        capFps: Float,
    ): Float

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

    /** Pins this renderer for an export (`geode_viz_prepare_offscreen`): offscreen tier, cut, fixed quality. */
    external fun vizPrepareOffscreen(handle: Long)

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

    /**
     * The catalogue's presets section (core/viz/CataloguePresets): the bundled looks and every bundled
     * preset, as its versioned line table (`geode_catalogue_presets_text`). Static; parse once (ui/BuiltInPresets does).
     */
    external fun cataloguePresetsText(): String

    /**
     * The scene params schema (core/viz/ParamTable.inc) as its versioned line table: per wire slot the
     * field's name, kind, default, control range and FADE/HOLD (`geode_params_schema_text`). Static; parse once.
     */
    external fun paramsSchemaText(): String

    /**
     * The config params schema (core/viz/ConfigTable.inc) as its versioned line table: per row the group
     * (lfo, adsr, macro, palette, look, clip, theme), its index in the group, name, kind, default, control
     * range and unit (`geode_config_schema_text`). The stores' clamps and defaults are to read it. Static; parse once.
     */
    external fun configSchemaText(): String

    /** A pinch's scale factor applied to Zoom, clamped to its schema range (core/viz/TouchTransform). */
    external fun touchZoom(
        current: Float,
        scaleFactor: Float,
    ): Float

    /** A twist in degrees added to Rotation, clamped to its schema range (core/viz/TouchTransform). */
    external fun touchRotation(
        current: Float,
        degrees: Float,
    ): Float

    /** Every rolled control as `TAB|ParamKeys string` lines, in roll order (core/viz/ParamRandomizer). Static. */
    external fun paramsRollKeys(): String

    /**
     * Rolls a new look (core/viz/ParamRandomizer) from [input], a SceneParamsCodec frame, into [output]:
     * the controls of [tab] (a ParamRandomizer roll group name; null = every group) that reach [styleId] (null = any),
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

    /**
     * The Customize panel's map (core/viz/CustomizeMap) as its versioned line table: the ten
     * categories, their Basics and Pro sections, and the controls in each. Static; parse once
     * (the app's CustomizeMapTable does).
     */
    external fun customizeText(): String

    /** Every control a roll can move (ParamKeys strings, one per line), so the panel offers to keep it; ask once. */
    external fun customizeLockableKeys(): String

    /** What the panel offers on [styleId]: `CATEGORY` and `CATEGORY/section` lines, in panel order. */
    external fun customizeShown(styleId: String): String

    /**
     * Rolls one Customize category (core/viz/CustomizeMap; [category] by its CustomizeMap name) from
     * [input] into [output]: its controls that reach [styleId] (null = any), except the newline-joined
     * ParamKeys in [locked]. Every other control holds. True when it changed something.
     */
    external fun customizeRandomize(
        styleId: String?,
        category: String,
        locked: String?,
        seed: Long,
        input: FloatArray,
        output: FloatArray,
    ): Boolean

    /** [input] with every field [category]'s controls write put back to its default, into [output]. */
    external fun customizeReset(
        category: String,
        input: FloatArray,
        output: FloatArray,
    )

    // Background image bake (core/viz/Underlay): pure, any thread, before vizSetUnderlay.

    /** The centred crop of a [srcW]x[srcH] picture to [targetW]:[targetH]'s aspect, as `[x, y, width, height]`. */
    external fun underlayCropRect(
        srcW: Int,
        srcH: Int,
        targetW: Int,
        targetH: Int,
    ): IntArray

    /** Blurs, then dims, [pixels] (ARGB, [width]x[height]) in place; 0 skips either step. */
    external fun underlayFinish(
        pixels: IntArray,
        width: Int,
        height: Int,
        blurRadius: Int,
        dim: Float,
    )

    /** The background controls' ranges: amount and dim 0..1 (NaN = 0), blur 0..[underlayBlurMax]. */
    external fun underlayClampAmount(amount: Float): Float

    external fun underlayClampDim(dim: Float): Float

    external fun underlayClampBlur(radius: Int): Int

    external fun underlayBlurMax(): Int

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

    // Encoder fallback ladder (core/output/EncoderLadder): what an export asks the video encoder for.

    /**
     * Every encoder attempt for [requestedFps] at [bitRate], in order, as `[codecIndex, fps, bitRate]`
     * triples: the ladder of each candidate codec in turn. [ceilings] has one entry per codec (at most
     * two), its listed upper rate for the size, 0 when unknown.
     */
    external fun encoderAttempts(
        requestedFps: Int,
        bitRate: Int,
        ceilings: IntArray,
    ): IntArray

    // Export loudness meter (core/audio/dsp/Loudness): BS.1770-4 integrated loudness and true peak.

    external fun loudnessSupports(channels: Int): Boolean

    /** 0 for a bad rate or a channel count [loudnessSupports] refuses. */
    external fun loudnessCreate(
        sampleRate: Int,
        channels: Int,
    ): Long

    external fun loudnessDestroy(handle: Long)

    /**
     * Feeds the decoder output [buffer]'s bytes [offset] until [offset] + [size] (BufferInfo's span) to the
     * meter where they lie: native-order s16, or float in -1..1 when [isFloat]. [buffer] must be direct, as
     * MediaCodec's are; false, feeding nothing, when it is not or the span does not fit.
     */
    external fun loudnessFeedDirect(
        handle: Long,
        buffer: java.nio.ByteBuffer,
        offset: Int,
        size: Int,
        isFloat: Boolean,
    ): Boolean

    external fun loudnessHasBlock(handle: Long): Boolean

    /**
     * Fills [out] (5 doubles: integrated LUFS, true peak dBTP, sample peak dBFS, measured ms, gated
     * blocks); returns false when nothing cleared the -70 LUFS gate, in which case out[0] is meaningless.
     */
    external fun loudnessFinish(
        handle: Long,
        out: DoubleArray,
    ): Boolean

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

    /** The requested frame rate clamped to what the export offers, 24..60. */
    external fun exportClampFps(requestedFps: Int): Int

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

    // PCM timing (core/audio/dsp/PcmTiming): the AAC encoder's rate, s16 durations, the start skip and trim.

    /** The AAC rate for [sourceRateHz]: its own up to 48 kHz; above, 44.1 kHz for the 44.1k family, else 48 kHz. */
    external fun aacRateFor(sourceRateHz: Int): Int

    /** End of an AAC track: [lastPtsUs] plus one 1024-sample frame at [sampleRateHz]. */
    external fun aacEndUs(
        lastPtsUs: Long,
        sampleRateHz: Int,
    ): Long

    /** Duration of [bytes] of interleaved s16 at [rateHz], the rate the bytes are at. */
    external fun pcmS16BytesToUs(
        bytes: Long,
        rateHz: Int,
        channels: Int,
    ): Long

    /** Interleaved samples before [us] at [rateHz]: what a start skip reads and discards. */
    external fun pcmSamplesBeforeUs(
        us: Long,
        rateHz: Int,
        channels: Int,
    ): Long

    /**
     * A decoded s16 buffer of [bytes] at [bufferPtsUs] trimmed to [startUs]: the bytes to drop from its
     * front (whole frames; == [bytes] when none is kept), with the kept part's pts (0 = the start) in
     * outPtsUs[0]. A buffer that straddles the start keeps the frames from the start on.
     */
    external fun pcmTrimS16(
        startUs: Long,
        bufferPtsUs: Long,
        bytes: Long,
        rateHz: Int,
        channels: Int,
        outPtsUs: LongArray,
    ): Long

    // Rest when silent (core/sound/Silence): the wallpaper's and the dream's silence rule.

    /** Whether a surface may rest: nothing heard for 6 s. */
    external fun silenceResting(
        nowMs: Long,
        heardAtMs: Long,
    ): Boolean

    // What the player tells the sound engines that the tap cannot hear (core/sound/PlayerSignals).

    /**
     * A new track (or the same one again) begins a new play with [seed]: every live renderer's sound engine
     * forgets the tempo, key and sections at its next frame and fires a full-strength section change, so a
     * crossfaded or gapless next track starts fresh. Returns the boundary's epoch. Any thread.
     */
    external fun soundTrackBoundary(seed: Long): Long

    /**
     * The player's fade gain, 0..1 (the UI fades times the crossfade), which the tap before the volume never
     * hears; the sound frame carries it as `fadeGain`, a reaction source. Live renderers only. Any thread.
     */
    external fun soundSetFadeGain(gain: Float)

    /**
     * Whether the player writes the app's ring (true) or the mic or another app's audio has taken it. While
     * the player is the source a pause holds the tempo, key and sections; the others keep the 2 s silence
     * re-warm, and read a fade gain of 1. Any thread.
     */
    external fun soundSetPlayerSource(on: Boolean)
    // Which sound the wallpaper and the dream hear (core/sound/SoundSource): the app's ring while fed,
    // else the output-mix capture when permitted, with hysteresis so it never flaps.

    /** Longs in the caller-held state of [soundSourceStep]; a zeroed array is a fresh one. */
    const val SOUND_SOURCE_STATE_LONGS: Int = 8

    /** Answer bit of [soundSourceStep]: read the capture (while it runs), not the ring. */
    const val SOUND_SOURCE_USE_CAPTURE: Int = 1

    /** Answer bit of [soundSourceStep]: try to start the capture now. */
    const val SOUND_SOURCE_START_CAPTURE: Int = 2

    /** Answer bit of [soundSourceStep]: stop the capture now. */
    const val SOUND_SOURCE_STOP_CAPTURE: Int = 4

    /**
     * One poll of the source rule: updates [state] in place and answers in `SOUND_SOURCE_*` bits.
     * [ringId] tells rings apart, [ringWritten] is its written-frame counter; [capturePermitted] is
     * the user's option plus the permission (or a capture already running).
     */
    external fun soundSourceStep(
        state: LongArray,
        nowMs: Long,
        ringPresent: Boolean,
        ringId: Long,
        ringWritten: Long,
        capturePermitted: Boolean,
        captureRunning: Boolean,
    ): Int

    /** Reports that a start [soundSourceStep] asked for failed, so the next try waits 10 s. */
    external fun soundSourceStartFailed(
        state: LongArray,
        nowMs: Long,
    )

    // Settings › Audio's listening tuning (core/sound/Tuning): the schema behind SoundTuning.

    /** Every `GeodeSoundTuning` slot's default, then every minimum, then every maximum; null when out of memory. */
    external fun soundTuningSchema(): FloatArray?

    /** Clamps [values] (the tuning's wire form) in place; a NaN or an infinity becomes the slot's default. */
    external fun soundTuningClamp(values: FloatArray)
    // Fixed-rate frame clock (core/viz/frame/FixedRateClock): Cast's pacing, PTS and audio cut.
    // The caller owns the state, a LongArray(FRAME_CLOCK_STATE), and reads now from System.nanoTime.

    /** Size of the frame clock's state (GEODE_FRAME_CLOCK_STATE). */
    const val FRAME_CLOCK_STATE = 3

    /** Starts [state] at [nowNs], [fps] frames a second; the first frame is due at once. */
    external fun frameClockStart(
        state: LongArray,
        fps: Int,
        nowNs: Long,
    )

    /** Nanoseconds until the next frame is due; 0 when it is due now. */
    external fun frameClockWaitNs(
        state: LongArray,
        nowNs: Long,
    ): Long

    /** Takes the frame to draw now and returns its PTS in ns; missed slots are dropped, never burst. */
    external fun frameClockTake(
        state: LongArray,
        nowNs: Long,
    ): Long

    /** The audio cut: the end of the last frame taken, in µs of stream time. */
    external fun frameClockAudioUntilUs(state: LongArray): Long

    /** The Cast stream's format into [out] (4 ints): width, height, fps, keyframe interval in seconds. */
    external fun castFormat(out: IntArray)
    // Export post-passes (core/viz/frame/ExportPasses): the watermark, a loop's seam, a Studio transition and
    // its capture. [assets] is the app's AssetManager (typed as Any, as for [vizCreate]); every call but the
    // three watermark sizes and the seam budget needs the export's GL context current, destroy included.

    /** "" when the pass built; otherwise why not (the handle must still be destroyed). */
    external fun exportPassError(handle: Long): String

    external fun exportPassDestroy(handle: Long)

    /** The wordmark's text size in pixels for a frame whose short side is [shortSide]. */
    external fun exportWatermarkTextPx(shortSide: Int): Float

    /** The power-of-two tile width for the wordmark measured at [textWidth] pixels. */
    external fun exportWatermarkTileWidth(textWidth: Float): Int

    external fun exportWatermarkTileHeight(tileWidth: Int): Int

    /** [rgba]: a direct buffer of the tile's premultiplied RGBA8 pixels, top row first (Bitmap.copyPixelsToBuffer). */
    external fun exportWatermarkCreate(
        assets: Any,
        rgba: java.nio.ByteBuffer,
        tileWidth: Int,
        tileHeight: Int,
    ): Long

    /** Over the default framebuffer, after the scene. */
    external fun exportWatermarkDraw(
        handle: Long,
        frameWidth: Int,
        frameHeight: Int,
    )

    external fun exportSeamCreate(
        assets: Any,
        width: Int,
        height: Int,
    ): Long

    /** Returns how many of [requested] stash frames fitted on the GPU. */
    external fun exportSeamAllocate(
        handle: Long,
        requested: Int,
    ): Int

    /**
     * After the scene drew source [frame] of a stop of [loopFrames] + [seamFrames]: stashes an opening frame
     * (returns -1, not presented) or blends the stash over a closing one, and returns the presentation time
     * in ns at [fps] (core/viz/frame/LoopSeam).
     */
    external fun exportSeamFrame(
        handle: Long,
        frame: Int,
        loopFrames: Int,
        seamFrames: Int,
        fps: Int,
    ): Long

    /** The crossfade a loop keeps under the seam stash's GPU budget; [crossfadeMs] when it fits. Pure. */
    external fun exportSeamBudgetedCrossfadeMs(
        crossfadeMs: Long,
        crossfadeFrames: Int,
        fps: Int,
        width: Int,
        height: Int,
    ): Long

    /**
     * The fraction of a whole loop render done once source [frame] of stop [stop] of [stops] is drawn,
     * rising in 0..1 and exactly 1 on the last frame (core/viz/frame/LoopSeam). Pure.
     */
    external fun exportLoopProgress(
        stop: Int,
        stops: Int,
        frame: Int,
        loopFrames: Int,
        seamFrames: Int,
    ): Float

    /** The crossfade a finished reel reports: [seamFrames] at [fps] in whole milliseconds. Pure. */
    external fun exportLoopReportedCrossfadeMs(
        seamFrames: Int,
        fps: Int,
    ): Long

    /** [id]: a GL Transitions name; an unknown one only copies. */
    external fun exportTransitionCreate(
        assets: Any,
        id: String,
        durationUs: Long,
    ): Long

    external fun exportTransitionConfigure(
        handle: Long,
        width: Int,
        height: Int,
    )

    /** [rgba]: a direct buffer, RGBA8 bottom row first; the first one is kept. */
    external fun exportTransitionSetFrom(
        handle: Long,
        rgba: java.nio.ByteBuffer,
        width: Int,
        height: Int,
    )

    /** Into the framebuffer and viewport Media3 bound for this program's output. */
    external fun exportTransitionDraw(
        handle: Long,
        inputTex: Int,
        ptsUs: Long,
    )

    external fun exportCaptureCreate(assets: Any): Long

    external fun exportCaptureConfigure(
        handle: Long,
        width: Int,
        height: Int,
    )

    external fun exportCaptureDraw(
        handle: Long,
        inputTex: Int,
    )

    /** Writes the last frame drawn (RGBA8, bottom row first) into the direct buffer [out]; false when none or it is too small. */
    external fun exportCaptureRead(
        handle: Long,
        out: java.nio.ByteBuffer,
    ): Boolean

    /**
     * The GL Transitions library as UTF-8 bytes, in the control-separated table of
     * `core/viz/TransitionLibrary.hpp`; read from the APK once per process, empty when missing.
     * [assets] is the app's `AssetManager`, typed as Any like [vizCreate]'s.
     */
    external fun transitionsTable(assets: Any): ByteArray

    // Settings › Performance (core/viz/frame/Quality): mode 0 Saver, 1 Balanced, 2 High.

    /** Floats [perfTier] writes: render scale, fps cap, march scale, post FX (0/1), adaptive (0/1). */
    const val PERF_TIER_FLOATS = 5

    /** Fills [out] with [mode]'s tier; false for an unknown mode. */
    external fun perfTier(
        mode: Int,
        out: FloatArray,
    ): Boolean

    /** The first-run mode for this phone; [totalRamBytes] 0 = unknown. */
    external fun perfModeForDevice(
        totalRamBytes: Long,
        lowRam: Boolean,
        mediaPerformanceClass: Int,
    ): Int
    // The live wallpaper's and the dream's run rule (core/viz/SurfaceRun). [flags] are the
    // SURFACE_* bits the shell observed; [animateUntilMs] is the wall-clock time the entitlement
    // lets the visual move until ([SURFACE_FOREVER] for Pro, a trial's end, 0 for free).

    /** 0 stopped, 1 a still frame, 2 moving. */
    external fun surfaceRunMode(
        flags: Int,
        animateUntilMs: Long,
        nowMs: Long,
    ): Int

    /** Whether the surface should be hearing sound (what wakes it from a silent rest). */
    external fun surfaceListening(
        flags: Int,
        animateUntilMs: Long,
        nowMs: Long,
    ): Boolean

    /** Whether the entitlement lets the visual move at [nowMs]; the dream ends when it stops. */
    external fun surfaceEntitled(
        animateUntilMs: Long,
        nowMs: Long,
    ): Boolean

    /** How long until the rule must be restated because a trial ends, or -1 for never. */
    external fun surfaceRecheckInMs(
        animateUntilMs: Long,
        nowMs: Long,
    ): Long

    /**
     * How long to keep pacing after a restate from [previousMode] to [mode] (the run modes' ordinals)
     * before stopping: the still frame's settle, or 0 when one frame, or plain on and off, is enough.
     */
    external fun surfaceSettleMs(
        previousMode: Int,
        mode: Int,
    ): Long

    /** A surface's stored fps choice as its pacer caps to: snapped to a listed rate, <= 0 the default. */
    external fun surfaceFpsChoice(requestedFps: Int): Int

    const val SURFACE_HAS_SURFACE = 1 shl 0
    const val SURFACE_VISIBLE = 1 shl 1
    const val SURFACE_PREVIEW = 1 shl 2
    const val SURFACE_COVERED = 1 shl 3
    const val SURFACE_BATTERY_SAVER = 1 shl 4
    const val SURFACE_PAUSE_IN_BATTERY_SAVER = 1 shl 5
    const val SURFACE_SILENT = 1 shl 6
    const val SURFACE_REST_WHEN_SILENT = 1 shl 7

    /** The system's Remove animations: a still frame (ADR 0008). */
    const val SURFACE_REDUCED_MOTION = 1 shl 8
    const val SURFACE_FOREVER = Long.MAX_VALUE

    /** The wallpaper's and the dream's fps before the user picks one (GEODE_SURFACE_DEFAULT_FPS). */
    const val SURFACE_DEFAULT_FPS = 30

    // The frame pacer (core/viz/frame/Pacer): which vsyncs a surface draws on, and the dt.

    /** Eight longs the caller owns, zeroed at first, stepped in place: the pacer's whole state. */
    const val PACER_STATE_LONGS = 8

    /** The state slot holding the cap the last vsync paced to, in millihertz (0 = none). */
    const val PACER_CAP_SLOT = 6

    /** The state slot holding the rate the last vsync really paced to, in millihertz (0 = every vsync). */
    const val PACER_PACED_SLOT = 7

    /**
     * One vsync: the dt in seconds to draw with, or 0 to skip it. [capFps] is the surface's own cap
     * (<= 0 draws every vsync); native paces to min([capFps], the thermal cap).
     */
    external fun pacerOnVsync(
        state: LongArray,
        frameTimeNanos: Long,
        capFps: Float,
    ): Double

    /** A start after a stop: forget the gap, keep the panel's period. */
    external fun pacerResume(state: LongArray)

    // The PCM ingest ring (core/audio/ingest/PcmRing): one lock-free ring per audio source. Every write
    // carries its rate, and the int16 and 8-bit conversions to float happen inside it.

    /** A ring of [capacityFrames] (a power of two) by [channels]; 0 for sizes it cannot take. Freed with [pcmRingDestroy]. */
    external fun pcmRingCreate(
        capacityFrames: Int,
        channels: Int,
        maxWriteFrames: Int,
    ): Long

    /** Drops the handle; the ring itself goes once no native cursor reads it. Never while a thread writes. */
    external fun pcmRingDestroy(handle: Long)

    /**
     * Writes the whole frames in [bytes] bytes of a direct [buffer] from [position], in the machine's byte
     * order; [encoding] is `AudioFormat.ENCODING_PCM_16BIT`, `_8BIT` or `_FLOAT`. Returns the frames
     * written, 0 for a heap buffer or an encoding it does not read.
     */
    external fun pcmRingWriteDirect(
        handle: Long,
        buffer: java.nio.ByteBuffer,
        position: Int,
        bytes: Int,
        encoding: Int,
        channels: Int,
        sampleRate: Int,
    ): Int

    /** Writes [frames] interleaved frames; returns the block's peak magnitude. */
    external fun pcmRingWriteFloats(
        handle: Long,
        samples: FloatArray,
        frames: Int,
        channels: Int,
        sampleRate: Int,
    ): Float

    /** [pcmRingWriteFloats] for 16-bit samples, scaled by 1/32768 natively. */
    external fun pcmRingWriteShorts(
        handle: Long,
        samples: ShortArray,
        frames: Int,
        channels: Int,
        sampleRate: Int,
    ): Float

    /** Writes the whole frames in the first [count] bytes of [samples], as [pcmRingWriteDirect] reads them. */
    external fun pcmRingWriteBytes(
        handle: Long,
        samples: ByteArray,
        count: Int,
        encoding: Int,
        channels: Int,
        sampleRate: Int,
    ): Int

    /** A new stream: every reader restarts at its next take. Any thread. */
    external fun pcmRingBeginEpoch(handle: Long)

    /** Frames written since the last [pcmRingBeginEpoch]. */
    external fun pcmRingWritten(handle: Long): Long

    /**
     * The rest-when-silent rule over the ring's newest [frames] frames: [nowMs] when any is above about
     * -42 dBFS, else [heardAtMs] (also when the ring has not written that many).
     */
    external fun pcmRingHeardAt(
        handle: Long,
        frames: Int,
        nowMs: Long,
        heardAtMs: Long,
    ): Long

    /** A reader's place in the ring at [ring]; [maxFrames] 0 is the ring's runway. Freed with [pcmCursorDestroy]. */
    external fun pcmCursorCreate(
        ring: Long,
        maxFrames: Int,
    ): Long

    external fun pcmCursorDestroy(handle: Long)

    /** The most frames one take hands out. */
    external fun pcmCursorMaxFrames(handle: Long): Int

    /**
     * What the ring gained since the last take, as interleaved 16-bit frames in [out] (which must hold
     * [pcmCursorMaxFrames] frames); returns the frames. [info] gets 1 at [0] on a restart and the rate the
     * frames were written at in [1].
     */
    external fun pcmCursorTakeS16(
        handle: Long,
        out: ShortArray,
        info: IntArray,
    ): Int

    // Playout clock (core/sound/interp/PlayoutClock, #349): the ring sample the speaker plays now.

    /** One clock per player, freed with [playoutDestroy]. */
    external fun playoutCreate(): Long

    external fun playoutDestroy(handle: Long)

    /** The sink handled a buffer at [presentationUs] that put [frames] ring samples from [ringFrame]. */
    external fun playoutBufferHandled(
        handle: Long,
        presentationUs: Long,
        ringFrame: Long,
        frames: Long,
    )

    /** The sink's position in media time (AudioTrack.getTimestamp, latency included), read at [nowNs]. */
    external fun playoutPosition(
        handle: Long,
        sinkUs: Long,
        nowNs: Long,
    )

    external fun playoutPlaying(
        handle: Long,
        playing: Boolean,
        nowNs: Long,
    )

    external fun playoutFlush(handle: Long)

    external fun playoutSpeed(
        handle: Long,
        speed: Float,
    )

    /** The user's A/V offset, clamped to ±1000 ms; positive shows the picture later. */
    external fun playoutSetOffsetMs(
        handle: Long,
        ms: Int,
    )

    /** The newest ring sample heard at [nowNs], never past the newest written; -1 with no map since a flush. */
    external fun playoutSpeakerFrame(
        handle: Long,
        nowNs: Long,
    ): Long

    // The playback output (core/audio/output): an Oboe stream under Media3's sink, where the AudioTrack stands.

    /** Whether the output plays [encoding] (`C.ENCODING_PCM_16BIT` or `_FLOAT`) at [channels] and [sampleRateHz]. */
    external fun audioOutSupports(
        encoding: Int,
        channels: Int,
        sampleRateHz: Int,
    ): Boolean

    /**
     * Opens a stream for writes of [encoding]; [bufferBytes] is the sink's buffer size for it, [sessionId]
     * the player's audio session (0 allocates one). 0 when the device gave no stream that is exactly this.
     * Ends with [audioOutRelease].
     */
    external fun audioOutOpen(
        sampleRateHz: Int,
        channels: Int,
        encoding: Int,
        bufferBytes: Int,
        sessionId: Int,
    ): Long

    /** Silences the stream and closes it off-thread; the handle is dead when this returns. */
    external fun audioOutRelease(handle: Long)

    external fun audioOutPlay(handle: Long)

    external fun audioOutPause(handle: Long)

    external fun audioOutFlush(handle: Long)

    /** The last frame was written: what waits plays out, and the position ends on it. */
    external fun audioOutStop(handle: Long)

    /**
     * Takes the whole frames that fit from [bytes] bytes of a direct [buffer] at [position], without
     * blocking. Returns the bytes taken; -1 for a heap buffer or once the stream has failed for good.
     */
    external fun audioOutWrite(
        handle: Long,
        buffer: java.nio.ByteBuffer,
        position: Int,
        bytes: Int,
    ): Int

    /** How much of what was written the listener has heard, in µs of audio; never backwards. */
    external fun audioOutPositionUs(handle: Long): Long

    /** 0..1, reached over a short ramp. */
    external fun audioOutSetVolume(
        handle: Long,
        volume: Float,
    )

    /** The device to play on (`AudioDeviceInfo.getId`), 0 for the default; a change reopens the stream. */
    external fun audioOutSetDevice(
        handle: Long,
        deviceId: Int,
    )

    /** Bits since the last call: 1 the position began to move, 2 an underrun, 4 the stream failed for good. */
    external fun audioOutEvents(handle: Long): Int

    external fun audioOutStalled(handle: Long): Boolean

    external fun audioOutSessionId(handle: Long): Int

    /** The output's buffer in frames: the FIFO plus the stream's own. */
    external fun audioOutBufferFrames(handle: Long): Long

    /** The cap a surface asking for [capFps] runs at now, under the thermal cap; 0 = none. For `Surface.setFrameRate`. */
    external fun pacerEffectiveCap(capFps: Float): Float

    // The process's one thermal state (core/viz/frame/Thermal); a reading is due on FRAME_THERMAL_DUE.

    /** One PowerManager reading: status (< 0 = no thermal API) and headroom (NaN = none this time). */
    external fun thermalPush(
        platformStatus: Int,
        headroom: Float,
    )

    /** Android's onTrimMemory level, as is; native acts on UI_HIDDEN and BACKGROUND and ignores the rest (#854). */
    external fun memoryTrim(level: Int)

    // Cast's audio pump (core/viz/frame/CastAudio): counts only; the caller keeps the PCM and the encoder.

    /** Size of the audio pump's state (GEODE_CAST_AUDIO_STATE): frames queued, frames held, rate. */
    const val CAST_AUDIO_STATE = 3

    /** Size of one step's result (GEODE_CAST_AUDIO_STEP): evict, skip, keep, take, pad, PTS in µs. */
    const val CAST_AUDIO_STEP = 6

    /** Starts [state] at [rateHz]; returns the hold's capacity in frames (half a second). */
    external fun castAudioStart(
        state: LongArray,
        rateHz: Int,
    ): Int

    /**
     * One frame of the pump: [gainedFrames] just resampled from the ring and the frame clock's cut
     * [untilUs]. Fills [out] (CAST_AUDIO_STEP longs), applied in order: evict the oldest held, skip at
     * the head of the gained, append the next keep, then queue take frames from the hold and pad
     * silence after them, stamped with the PTS.
     */
    external fun castAudioStep(
        state: LongArray,
        gainedFrames: Int,
        untilUs: Long,
        out: LongArray,
    )
    // Cast's live HLS timing (core/viz/frame/HlsSegments): media-time segment durations and the playlist.

    /** Segments kept and listed (GEODE_HLS_WINDOW). */
    external fun hlsWindow(): Int

    /** Seconds of media time the fragment (moof, with or without its mdat) plays, read with [init]'s timescales; 0 = unknown. */
    external fun hlsFragmentSeconds(
        init: ByteArray,
        fragment: ByteArray,
    ): Double

    /** The live playlist for [durations] (0 = unknown) numbered from [firstSequence], or null until enough are listed. */
    external fun hlsPlaylist(
        firstSequence: Long,
        durations: DoubleArray,
        initUri: String,
        segmentPrefix: String,
        segmentSuffix: String,
    ): String?
}
