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
     * Voice "play X" and Android Auto's search over the library (core/library/Search), best first. [rows]
     * holds three UTF-8 strings per track (title, artist, album) in library order; [request] the query,
     * then MediaStore's focus, artist, album and title extras (null = absent). Writes matching track indices into [out] and returns how many
     * match in all; nothing asked matches every track, 0 means no match.
     */
    external fun librarySearch(
        rows: Array<ByteArray?>,
        request: Array<ByteArray?>,
        out: IntArray,
    ): Int

    /**
     * The Library screen's index (core/library/Search): [rows] as in [librarySearch], one duration and one
     * added time per track. Built once per library change; 0 when it cannot be built. Freed with
     * [libraryIndexDestroy].
     */
    external fun libraryIndexCreate(
        rows: Array<ByteArray?>,
        durationsMs: LongArray,
        addedSec: LongArray,
    ): Long

    external fun libraryIndexDestroy(handle: Long)

    /** Every row of the index in [sort] order: 0 title, 1 artist, 2 album, 3 duration, 4 newest first. */
    external fun libraryIndexOrder(
        handle: Long,
        sort: Int,
    ): IntArray

    /**
     * What a UTF-8 [query] finds, best first: three counts (tracks, albums, artists), then those rows, an
     * album or artist as its first row. A blank query finds nothing.
     */
    external fun libraryIndexSearch(
        handle: Long,
        query: ByteArray,
    ): IntArray

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

    /** Settings › Touch › Smear (`core/viz/TouchSmear`): the switch and the strength, clamped natively. */
    external fun vizSetTouchSmear(
        handle: Long,
        on: Boolean,
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

    external fun vizSetSoundTuning(
        handle: Long,
        tuning: FloatArray,
    )

    /** The reaction source catalogue (core/viz/Reactions.cpp): a "NAME|Label|GROUP|0/1" line per source, in wire order. */
    external fun reactSourceCatalog(): String

    /**
     * The macro bank (core/viz/MacroBank), as the text the store keeps: [macroBankSet] makes it the one
     * every renderer in the process adds to its params; the others return a new bank text and decide
     * everything (unreadable text is the default bank, a refused edit the bank unchanged).
     */
    external fun macroBankSet(bank: String)

    external fun macroBankDefault(): String

    external fun macroBankNormalize(bank: String): String

    external fun macroBankSetValue(
        bank: String,
        knobId: Int,
        value: Float,
    ): String

    external fun macroBankRename(
        bank: String,
        knobId: Int,
        name: String,
    ): String

    external fun macroBankAddKnob(
        bank: String,
        name: String,
    ): String

    /** Refused for a built-in knob, which [macroBankResetKnob] puts back as built instead. */
    external fun macroBankRemoveKnob(
        bank: String,
        knobId: Int,
    ): String

    external fun macroBankDuplicateKnob(
        bank: String,
        knobId: Int,
    ): String

    external fun macroBankResetKnob(
        bank: String,
        knobId: Int,
    ): String

    /** The knob [delta] places along the bank, negative earlier. */
    external fun macroBankMoveKnob(
        bank: String,
        knobId: Int,
        delta: Int,
    ): String

    /** [target] is a modulation target's wire name; [up] and [down] are -1..1 of its range at a full turn each way. */
    external fun macroBankSetLink(
        bank: String,
        knobId: Int,
        target: String,
        up: Float,
        down: Float,
    ): String

    /** Links a target the knob does not move yet, at the weights native starts a link on. */
    external fun macroBankAddLink(
        bank: String,
        knobId: Int,
        target: String,
    ): String

    external fun macroBankRemoveLink(
        bank: String,
        knobId: Int,
        target: String,
    ): String

    /** The knob editor's one read on [styleId]: the caps, each knob with what it offers, its links, every target. */
    external fun macroBankEditor(
        bank: String,
        styleId: String,
    ): String

    /** How a link follows the knob: [curve] is LINEAR, EASE_IN, EASE_OUT or S_CURVE. */
    external fun macroBankSetCurve(
        bank: String,
        knobId: Int,
        target: String,
        curve: String,
    ): String

    /** A new knob that moves the Customize control [control] (a ParamKeys name); native names it. */
    external fun macroBankAddKnobFor(
        bank: String,
        control: String,
    ): String

    /** Every Customize control a knob can move, its category, and the knobs that move it now. */
    external fun macroBankControls(bank: String): String

    /** A random set of links for the knob in place of its own; the same [seed], the same set. */
    external fun macroBankRoll(
        bank: String,
        knobId: Int,
        seed: Long,
    ): String

    /**
     * What the waves and envelopes on a knob add to its turn on the on-screen surface's last frame, one
     * `mod|<knob id>|<add>|<sources: 1 wave, 2 envelope, 3 both>` row per knob; the knob's second arc polls it.
     */
    external fun macroBankLiveMods(): String

    /** New ranges and curves for the links the knob has, their params kept; the same [seed], the same ranges. */
    external fun macroBankRollRanges(
        bank: String,
        knobId: Int,
        seed: Long,
    ): String

    /** Up to [count] more params on the knob, picked at random among those [styleId] reaches and Customize does not keep. */
    external fun macroBankAddRandom(
        bank: String,
        knobId: Int,
        styleId: String,
        count: Int,
        seed: Long,
    ): String

    /** A new turn for every knob on screen that moves something on [styleId]; the same [seed], the same turns. */
    external fun macroBankRollValues(
        bank: String,
        styleId: String,
        seed: Long,
    ): String

    /** The knob off screen into the bank's library, centred. */
    external fun macroBankHideKnob(
        bank: String,
        knobId: Int,
    ): String

    /** A knob from the library back on screen, last; refused when the screen is full. */
    external fun macroBankShowKnob(
        bank: String,
        knobId: Int,
    ): String

    /** The name native offers when the knob is saved (a name, not a bank). */
    external fun macroBankAutoName(
        bank: String,
        knobId: Int,
    ): String

    /**
     * The auto-director (core/viz/Director): the music's feeling decides behaviour (axes, camera, motion)
     * over every style; never a colour, never a route. [on] is the user's switch as set, which an export directs by ([vizPinEngineSettings]);
     * [amount] 0 off .. 1 full is what live surfaces drive with now; [seed] is the play's, so a new seed
     * starts the song afresh and an export pinned with it makes the same film.
     */
    external fun directorSet(
        on: Boolean,
        amount: Float,
        seed: Long,
    )

    /**
     * How far the director reaches past its look (core/viz/DirectorReach): 0 the look alone, 1 it also
     * shuffles Customize categories at a new section or a drop, 2 it also runs slow modulators of its own.
     * Process-wide like [directorSet] and pinned with it by an export; nothing happens while Auto is off.
     */
    external fun directorReachSet(level: Int)

    /**
     * Customize's keep set, which the director's reach never moves: one control key or `#CATEGORY` per
     * line, the lock set [paramsRandomize] takes.
     */
    external fun directorKeepSet(keys: String)

    /**
     * Note & key reactions (core/viz/NoteResponse): the notes, chords, key and mood drive the six style
     * knobs each style spends its own way; no scene param, no colour. Process-wide like
     * the director, so the screen, an export, the wallpaper and the dream agree; off is exactly the picture
     * without it. The play's seed is the director's ([directorSet]). A live renderer mixes fresh entropy into it per play,
     * so the live note response differs every time; an export pins its settings with none
     * ([vizPinEngineSettings]), so exporting the same play twice draws the same picture, though not the one
     * the screen drew.
     */
    external fun noteResponseSet(enabled: Boolean)

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
     * The user's reaction [routes] (`REACT_ROUTE_FLOATS` per route; null: none, the DNA plays)
     * (geode_viz_set_user_reactions). Native resolves what plays from these, the scene, `reactSeed` and the hand
     * mask ([handSet], or an export's pin), which it reads itself. Any thread.
     */
    external fun vizSetUserReactions(
        handle: Long,
        routes: FloatArray?,
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

    /** The saved text of the user's [routes] (core/viz/ReactionText), in the [vizSetUserReactions] layout. */
    external fun reactionsTextEncode(routes: FloatArray): String

    /** The routes saved as [text], in the [vizSetUserReactions] layout; null for text that is no list. */
    external fun reactionsTextDecode(text: String): FloatArray?

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

    /** One game rotation vector reading (quaternion) on a screen at [displayRotation] (Surface.ROTATION_*), latched for the next [vizFrame]. Any thread. */
    external fun vizGyroPush(
        handle: Long,
        x: Float,
        y: Float,
        z: Float,
        w: Float,
        displayRotation: Int,
    )

    /** The gyro stopped: the next frame recentres the look; the next reading starts a new baseline. Any thread. */
    external fun vizGyroOff(handle: Long)

    /** The style a live surface draws, latched: each [vizFrame] asks for it until the renderer can draw it. Any thread. */
    external fun vizRequestScene(
        handle: Long,
        sceneId: String,
    )

    /** Settings › Performance, latched for the next [vizFrame]; [liquidHotPolicy] is `liquid::HotPolicy`. Any thread. */
    external fun vizSetQuality(
        handle: Long,
        renderScale: Float,
        marchScale: Float,
        postFx: Boolean,
        adaptive: Boolean,
        liquidHotPolicy: Int,
    )

    /**
     * One whole live frame into framebuffer 0 (geode_viz_frame, `core/viz/frame/LiveFrame`), GL thread: the
     * renderer hears [ring] (0: nothing) and draws with what was pushed to it. [pacedFps] is the rate this
     * surface's pacer last drew at (0 free-running) and [capFps] the surface's own cap (the same for a fixed
     * rate). Answers the `FRAME_*` events; act on a bit only when it is set.
     */
    external fun vizFrame(
        handle: Long,
        ring: Long,
        pacedFps: Float,
        capFps: Float,
    ): Int

    /** What Liquid mode ran when a frame last said [FRAME_LIQUID_CHANGED]: 0 off, 1 full, 2 lite, 3 warp, 4 paused. */
    external fun vizLiquidMode(handle: Long): Int

    /** Mirrors GEODE_FRAME_CLEAR_ONLY: native cleared a new surface's first frame; present it, do nothing else. */
    const val FRAME_CLEAR_ONLY: Int = 0x1

    /** Mirrors GEODE_FRAME_LIQUID_CHANGED: Liquid mode changed; read it with [vizLiquidMode]. */
    const val FRAME_LIQUID_CHANGED: Int = 0x4

    /** Mirrors GEODE_FRAME_ERROR_CHANGED: the error text changed; read it with [vizLastError]. */
    const val FRAME_ERROR_CHANGED: Int = 0x8

    /** Mirrors GEODE_FRAME_MILK_LOADED: MilkDrop finished loading a preset; take it with [vizTakeMilkPresetLoaded]. */
    const val FRAME_MILK_LOADED: Int = 0x10

    /** Mirrors GEODE_FRAME_DIRECTOR_MOMENT: take the moments with [vizTakeDirectorStyleMoments]. */
    const val FRAME_DIRECTOR_MOMENT: Int = 0x20

    /** Mirrors GEODE_FRAME_STILL_READY: the still asked for is ready ([vizStillSize], [vizStillRead]). */
    const val FRAME_STILL_READY: Int = 0x40

    /**
     * Pins an export renderer's engine settings once (geode_viz_pin_engine_settings, #350): from then on it
     * ignores [macroBankSet], [noteResponseSet], [handSet] and [directorSet], so live edits never reach a render
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

    /** Sheds the renderer's spare scenes as a UI_HIDDEN trim does; GL thread, before it pauses (`geode_viz_trim_spares`). */
    external fun vizTrimSpares(handle: Long)

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
     * preset, as its versioned line table (`geode_catalogue_presets_text`). Static; parse once (data/BuiltInPresets does).
     */
    external fun cataloguePresetsText(): String

    /**
     * The scene params schema (core/viz/ParamTable.inc) as its versioned line table: per wire slot the
     * field's name, kind, default, control range and FADE/HOLD (`geode_params_schema_text`). Static; parse once.
     */
    external fun paramsSchemaText(): String

    /**
     * The hue base a palette slot draws: [override] when it is >= 0, else built-in palette [palette]'s, the
     * index clamped into the table (`geode_params_palette_base`, core/viz/Params). Pure.
     */
    external fun paramsPaletteBase(
        palette: Int,
        override: Float,
    ): Float

    /** [paramsPaletteBase] for the hue span (`geode_params_palette_range`). */
    external fun paramsPaletteRange(
        palette: Int,
        override: Float,
    ): Float

    /** Your colours (core/viz/Colour.hpp): a packed 0xRRGGBB from hue (wraps), saturation and value (`geode_colour_from_hsv`). Pure. */
    external fun colourFromHsv(
        h: Float,
        s: Float,
        v: Float,
    ): Int

    /** Hue, saturation and value (0..1) of a packed 0xRRGGBB into [out] (`geode_colour_to_hsv`). Pure. */
    external fun colourToHsv(
        rgb: Int,
        out: FloatArray,
    )

    /** "#RRGGBB", "RRGGBB", "#RGB" or "RGB" as a packed colour, -1 if it is none (`geode_colour_parse_hex`). Pure. */
    external fun colourParseHex(text: String): Int

    /** A packed colour as "#RRGGBB" (`geode_colour_hex`). Pure. */
    external fun colourHex(rgb: Int): String

    /** The mean of an Android bitmap's ARGB pixels, alpha ignored: the eyedropper (`geode_colour_average_argb`). Pure. */
    external fun colourAverageArgb(argb: IntArray): Int

    /** A cover's ARGB pixels as [base hue, span, confidence], or null when none is coloured (`geode_art_palette_extract`). Pure. */
    external fun artPaletteExtract(argb: IntArray): FloatArray?

    /** Colour map [index]'s name (`paletteLut` [index]), "" past the last (`geode_palette_colour_map_name`). Pure. */
    external fun paletteColourMapName(index: Int): String

    /** [samples] colours along your colours' loop through [stops] (`geode_palette_ramp`). Pure. */
    external fun paletteRamp(
        stops: IntArray,
        samples: Int,
    ): IntArray

    /** A hue gradient as [count] (<= 5) stops, where the loop puts them (`geode_palette_stops_from_hues`). Pure. */
    external fun paletteStopsFromHues(
        base: Float,
        range: Float,
        count: Int,
    ): IntArray

    /** [samples] colours of what a params frame's slot paints, its stops or its hue ramp (`geode_params_palette_preview`). Pure. */
    external fun paramsPalettePreview(
        params: FloatArray,
        second: Boolean,
        samples: Int,
    ): IntArray

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
     * Randomize for the waves (core/viz/WaveRandomizer): [input] and [output] are the three slots in
     * ModConfigCodec.packLfo's layout, one after another; [params] is the look on screen. [runningOnly] false is
     * the wave editor's button (every wave on and rolled); true is a category roll, which only re-rolls the
     * steady waves already on and none when [locked] keeps them. True when it changed something.
     */
    external fun wavesRandomize(
        styleId: String?,
        locked: String?,
        seed: Long,
        runningOnly: Boolean,
        params: FloatArray,
        input: FloatArray,
        output: FloatArray,
    ): Boolean

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

    /** [input] with every field [category]'s controls write taken from the look in [from], into [output]. */
    external fun customizeBorrow(
        category: String,
        input: FloatArray,
        from: FloatArray,
        output: FloatArray,
    )

    /**
     * Randomize everything (core/viz/RandomizeAll), the two-circle gesture: a style among every catalogue
     * style not in [lockedStyles] (newline-joined ids), not [current] while another is open, MilkDrop
     * only when [milkCount] > 0. [picks] gets the index into [lookStyles] (each preset's style id, one
     * per line) of a preset made for it, or -1, then on MilkDrop with no preset a .milk index, or -1.
     * Null when no style is open. Pass the same [seed] to [randomizeAllRoll].
     */
    external fun randomizeAllPick(
        seed: Long,
        current: String,
        lockedStyles: String,
        lookStyles: String,
        milkCount: Int,
        picks: IntArray,
    ): String?

    /**
     * [base] (the look once the pick is applied) with the controls [locked] keeps put back to their
     * values in [before], then, with [rollCategories], every Customize category's other controls that
     * reach [styleId] rolled, into [output]. True when it changed [base].
     */
    external fun randomizeAllRoll(
        styleId: String,
        locked: String,
        seed: Long,
        rollCategories: Boolean,
        before: FloatArray,
        base: FloatArray,
        output: FloatArray,
    ): Boolean

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

    // Export planning and PCM maths (core/output, audio/dsp/PcmConvert and PcmTiming). Pure functions, any
    // thread; the Kotlin exporters only marshal through these.

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

    // An export's render (core/output/ExportFrame, core/api/geode_export_job_api.h): the prepare order, the
    // pre-roll and each frame's audio and draw are native; the shell hands over the renderer, the track's
    // descriptor and the look. Every call on the thread holding the renderer's GL context.

    /**
     * A render over renderer [viz] (a `geode_viz` handle, borrowed: destroy the job first) of the track at [fd],
     * which native takes and closes, also when it returns 0. [length] >= 0 is the span from [offset] an
     * AssetFileDescriptor declares; < 0 the whole descriptor. Freed with [exportJobDestroy].
     */
    external fun exportJobCreate(
        viz: Long,
        fd: Int,
        offset: Long,
        length: Long,
        fps: Int,
        rangeStartMs: Long,
    ): Long

    external fun exportJobDestroy(job: Long)

    /**
     * The prepare order up to the look (offscreen pin, frame rate, surface, scene, [milkPath], engine settings
     * pinned for [pinDurationMs]): the pinned hand mask the look's reactions go under, or -1 when the renderer
     * cannot draw [sceneId].
     */
    external fun exportJobBegin(
        job: Long,
        width: Int,
        height: Int,
        sceneId: String,
        milkPath: String?,
        entitled: Boolean,
        pinDurationMs: Long,
    ): Long

    /** Once the look is applied: the pre-roll and the range's decode; false when the track's audio was not heard. */
    external fun exportJobWarm(job: Long): Boolean

    /** Frame [frame] of the range into [targetFbo]: its audio, then its draw. False when the decoder failed. */
    external fun exportJobFrame(
        job: Long,
        frame: Long,
        targetFbo: Int,
    ): Boolean

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

    /**
     * What an export's measured loudness asks of platform [target] (0 Leave as-is, 1 YouTube, 2 Shorts;
     * core/output/LoudnessAdvice): returns the kind (0 nothing to measure, 1 as mixed, 2 on target,
     * 3 normalise) and fills [out] (9 doubles): gain dB, gain the level alone needed, resulting LUFS and true
     * peak, direction (1, -1, 0), ceiling limited (1/0), band (-1 under, 1 over, 0 in), LU under YouTube worth
     * saying (else 0), peak over YouTube's ceiling (1/0). [gated] false: nothing above the meter's gate.
     */
    external fun loudnessAdvise(
        target: Int,
        gated: Boolean,
        lufs: Double,
        truePeakDbtp: Double,
        out: DoubleArray,
    ): Int

    /** [target]'s window, aim, low, high LUFS and true-peak ceiling into [out]; false when it does not normalise. */
    external fun loudnessWindow(
        target: Int,
        out: DoubleArray,
    ): Boolean

    /**
     * A time-of-day drift's stops as (hue offset, warmth offset) pairs, none at the first and the whole
     * drift at the last; one pair of zeros when it does not move (core/output/LoopPlan).
     */
    external fun loopDriftStops(
        hueTurns: Float,
        warmth: Float,
        stops: Int,
    ): FloatArray

    /** One drift stop on a look: [out] gets the wrapped colorShift, then the temperature held to -1..1. */
    external fun loopDriftApply(
        colorShift: Float,
        temperature: Float,
        hueOffset: Float,
        warmthOffset: Float,
        out: FloatArray,
    )

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

    /** Why a recording ended ([recorderStopped]); 0 while it runs. `Stop` in core/output/Recorder. */
    const val RECORD_STOP_USER = 1
    const val RECORD_STOP_MAX_LENGTH = 2
    const val RECORD_STOP_RESIZED = 3
    const val RECORD_STOP_FILE_FULL = 4
    const val RECORD_STOP_FAILED = 5

    /** A record session's state ([recordSessionState]): stopped, the tee should close; then finished, saved or (3) not. */
    const val RECORD_SESSION_CLOSING = 1
    const val RECORD_SESSION_SAVED = 2

    /** The monotonic clock every `nowNs` of a recorder is read from, in ns. */
    external fun recordNowNs(): Long

    /**
     * Record's clock (core/output/Recorder) for a stage of this size: which frames go into the file and
     * their PTS, where each heard sample sits, when it ends. [maxSec] is Settings › Export › Record's
     * maximum and [capMs] the entitlement's cap; 0 is none. 0 when it could not be made.
     */
    external fun recorderCreate(
        stageWidth: Int,
        stageHeight: Int,
        fps: Int,
        maxSec: Int,
        capMs: Long,
    ): Long

    external fun recorderDestroy(handle: Long)

    /** Ends the recording for [why], one of `RECORD_STOP_*`; the first reason wins. */
    external fun recorderStop(
        handle: Long,
        why: Int,
        nowNs: Long,
    )

    /** 0 while it runs, else the `RECORD_STOP_*` it ended for. */
    external fun recorderStopped(handle: Long): Int

    /** The length so far, or the final length once stopped. */
    external fun recorderRecordedMs(
        handle: Long,
        nowNs: Long,
    ): Long

    /** The stage frames left out because the encoder was behind. */
    external fun recorderFramesDropped(handle: Long): Long

    /** The longest this recording may run, the shorter of its two limits, in ms; 0 for none. */
    external fun recorderLimitMs(handle: Long): Long

    /** Whether a recording [recordedMs] long is kept (a shorter one is a mis-tap). */
    external fun recordWorthKeeping(recordedMs: Long): Boolean

    /**
     * A recording's encoders and file (`geode_record_session_open`): H.264 at the first size the device takes, AAC,
     * and the MP4 muxer on [fd] (read-write; the muxer keeps its own duplicate), on [recorder]'s clock and the
     * session's own encoder thread. 0 when no encoder would start.
     */
    external fun recordSessionOpen(
        recorder: Long,
        fd: Int,
        stageWidth: Int,
        stageHeight: Int,
        shortSide: Int,
        fps: Int,
    ): Long

    /** Once [recordSessionState] is `RECORD_SESSION_SAVED` or `_FAILED` and the GL thread's [recordSessionTeeClosed] ran. */
    external fun recordSessionDestroy(session: Long)

    /** The encoder's frame into [out]: width, height. */
    external fun recordSessionSize(
        session: Long,
        out: IntArray,
    )

    /** One of `RECORD_SESSION_*`, polled. */
    external fun recordSessionState(session: Long): Int

    /** GL thread, after [vizRecordStop]: the stage no longer draws into the encoder, so its stream may end. */
    external fun recordSessionTeeClosed(session: Long)

    /**
     * GL thread. From the next [vizRender] on, the frames the session's clock keeps are copied into its encoder
     * and the sound the renderer hears goes to the clock. [watermark] is an `exportWatermarkCreate` handle built on
     * this thread, or 0. False when the driver would not open the encoder's surface or the session already ended.
     */
    external fun recordSessionTeeStart(
        session: Long,
        handle: Long,
        watermark: Long,
    ): Boolean

    /** GL thread, or after its surface went: ends the copy. */
    external fun vizRecordStop(handle: Long)

    /**
     * Marks this renderer as the stage the cast carries (`geode_viz_set_cast_source`, core/viz/frame/CastTee): while a
     * cast's tee is open it copies its finished visual into it, at most once per cast frame. Any thread; the app's stage only.
     */
    external fun vizSetCastSource(
        handle: Long,
        on: Boolean,
    )

    /**
     * The cast thread, its EGL context current: opens the tee the stage copies into, [width] x [height] at [fps];
     * 0 when the driver lacks what it needs (the cast then renders on its own).
     */
    external fun castTeeOpen(
        width: Int,
        height: Int,
        fps: Int,
    ): Long

    /** The cast thread: blits the stage's newest copy into the encoder surface; false while the stage draws nothing. */
    external fun castTeeDraw(
        tee: Long,
        nowNs: Long,
        width: Int,
        height: Int,
    ): Boolean

    /** The cast thread, its context still current: closes the tee. */
    external fun castTeeClose(tee: Long)

    /** True while a new stream leaves this slot empty for the stage's first copy (`geode_cast_tee_await_stage`). Pure. */
    external fun castTeeAwaitStage(
        stageDraws: Boolean,
        fallbackMade: Boolean,
        sinceOpenNs: Long,
    ): Boolean

    /** Shake to screenshot: read the next finished visual back once (`geode_viz_still_request`). Any thread. */
    external fun vizStillRequest(handle: Long)

    /** GL thread: the still's width and height into [out], true once it is ready. */
    external fun vizStillSize(
        handle: Long,
        out: IntArray,
    ): Boolean

    /** GL thread: the still, RGBA8 rows top first, into the direct buffer [out]; then it is let go. */
    external fun vizStillRead(
        handle: Long,
        out: java.nio.ByteBuffer,
    ): Boolean

    /** Tap along (`core/viz/TapAlong`): a touch-down at [uptimeNs]; true on the tap that completes a streak on the beat. Any thread. */
    external fun vizTapAlong(
        handle: Long,
        uptimeNs: Long,
    ): Boolean

    /** Shake to screenshot's detector (`core/viz/Shake`), one per listener. */
    external fun shakeCreate(): Long

    external fun shakeDestroy(handle: Long)

    /** One TYPE_ACCELEROMETER event; true on the sample that completes a shake. */
    external fun shakeSample(
        handle: Long,
        timeNs: Long,
        x: Float,
        y: Float,
        z: Float,
    ): Boolean

    /** The maximum take lengths Settings › Export › Record offers, in seconds; 0 is "no limit". */
    external fun recordMaxLengths(): IntArray

    /** A stored maximum take length made one of [recordMaxLengths]. */
    external fun recordSnapMaxLength(requestedSec: Int): Int

    /** The bytes [lengthSec] of video at [videoBps] takes with its AAC, the muxer's target applied. */
    external fun recordEstimateBytes(
        videoBps: Int,
        lengthSec: Int,
    ): Long

    /** Whether a render that long lowers [videoBps] to stay under the muxer's target. */
    external fun recordRateLowered(
        videoBps: Int,
        lengthSec: Int,
    ): Boolean

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

    /** A capture's last sound: [nowMs] when [peak] (a ring write's answer) is above digital zero, else [heardAtMs]. */
    external fun captureHeardAt(
        peak: Float,
        nowMs: Long,
        heardAtMs: Long,
    ): Long

    /** Whether a capture has heard only digital zero for 4 s since [heardAtMs]. */
    external fun captureQuiet(
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

    /** Ints [castFormat] fills (GEODE_CAST_FORMAT_INTS). */
    const val CAST_FORMAT_INTS = 6

    /** The Cast stream's format into [out] ([CAST_FORMAT_INTS] ints): width, height, fps, keyframe interval (s), video and audio bit/s. */
    external fun castFormat(out: IntArray)
    // Export post-passes (core/viz/frame/ExportPasses): the watermark and a loop's seam. [assets] is the
    // app's AssetManager (typed as Any, as for [vizCreate]); every call but the watermark's pure calls
    // (placement to tile size) and the seam budget needs the export's GL context current, destroy included.

    /** "" when the pass built; otherwise why not (the handle must still be destroyed). */
    external fun exportPassError(handle: Long): String

    external fun exportPassDestroy(handle: Long)

    /**
     * Whether a render's frames carry the wordmark and where (`exportmath::watermark::placement`): 0 for
     * none, else the placement code (`dev.geode.model.WatermarkChoice`), an end card becoming a corner
     * mark on frames of a [use] whose end is not known.
     */
    external fun exportWatermarkPlacement(
        required: Boolean,
        proAdds: Boolean,
        placement: Int,
        use: Int,
    ): Int

    /** The wordmark's text size in pixels for a frame whose short side is [shortSide]. */
    external fun exportWatermarkTextPx(
        shortSide: Int,
        placement: Int,
    ): Float

    /** The tile's rotation in degrees, and the text's alpha (0..255). */
    external fun exportWatermarkAngle(placement: Int): Float

    external fun exportWatermarkAlpha(placement: Int): Int

    /** The tile width for the wordmark measured at [textWidth] pixels (a power of two for the tiled pattern). */
    external fun exportWatermarkTileWidth(
        textWidth: Float,
        textPx: Float,
        placement: Int,
    ): Int

    external fun exportWatermarkTileHeight(
        tileWidth: Int,
        textPx: Float,
        placement: Int,
    ): Int

    /**
     * [rgba]: a direct buffer of the tile's premultiplied RGBA8 pixels, top row first (Bitmap.copyPixelsToBuffer).
     * [lengthMs] is the video's, for an end card; 0 when unknown.
     */
    external fun exportWatermarkCreate(
        assets: Any,
        rgba: java.nio.ByteBuffer,
        tileWidth: Int,
        tileHeight: Int,
        placement: Int,
        corner: Int,
        lengthMs: Long,
    ): Long

    /** Over the default framebuffer, after the scene; [timeMs] is the frame's time in the video. */
    external fun exportWatermarkDraw(
        handle: Long,
        frameWidth: Int,
        frameHeight: Int,
        timeMs: Long,
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

    /** Mirrors GEODE_LIVE_FPS_MAX_CHOICES: the size of the array [liveFpsChoices] fills. */
    const val LIVE_FPS_MAX_CHOICES = 5

    /**
     * Mirrors GEODE_LIVE_FPS_FOLLOW: Settings › Performance's "Follow device", stored as the live cap and handed
     * to the pacer as one. The stage runs at the rate the display runs now and states no rate of its own.
     */
    const val LIVE_FPS_FOLLOW = -1

    /**
     * Settings › Performance's live frame-rate caps (core/viz/frame/LiveFps) on a display that can switch to
     * [refreshRates] seamlessly, ascending into [out]; returns how many. 30 and 60 always, 90 and 120 where
     * the display shows them, then [LIVE_FPS_FOLLOW]. The in-app stage only: the wallpaper, dream, export and
     * Cast keep their own.
     */
    external fun liveFpsChoices(
        refreshRates: FloatArray,
        out: IntArray,
    ): Int

    /** The stored live cap as a display with [refreshRates] runs it: offered as is, else the nearest offered. */
    external fun liveFpsCap(
        storedFps: Int,
        refreshRates: FloatArray,
    ): Int

    /** The first-run mode for this phone; [totalRamBytes] 0 = unknown. */
    external fun perfModeForDevice(
        totalRamBytes: Long,
        lowRam: Boolean,
        mediaPerformanceClass: Int,
    ): Int

    /**
     * The app's own stage (core/viz/StageRun): [flags] are the STAGE_* bits the shell observed; the answer has
     * [STAGE_DRAWS] while somebody can see the stage and [STAGE_CAST_LEADS_AUTO] while a cast's renderer
     * counts the auto-director's style moments in its place.
     */
    external fun stageRun(flags: Int): Int

    const val STAGE_STARTED = 1 shl 0
    const val STAGE_RESUMED = 1 shl 1
    const val STAGE_PIP = 1 shl 2
    const val STAGE_SECOND_SCREEN = 1 shl 3
    const val STAGE_CASTING = 1 shl 4
    const val STAGE_DRAWS = 1 shl 0
    const val STAGE_CAST_LEADS_AUTO = 1 shl 1

    /**
     * Where the stage's view goes (core/viz/StageRun host): [flags] are STAGE_HOST_SHOWN / _CASTING, [bounds] the
     * Visuals rect (left, top, right, bottom; empty before it was laid out), the window [windowW] x [windowH];
     * [rectOut] gets the view's rect. Returns STAGE_HOST_HIDDEN, _FAINT or _OPAQUE.
     */
    external fun stageHost(
        flags: Int,
        bounds: IntArray,
        windowW: Int,
        windowH: Int,
        rectOut: IntArray,
    ): Int

    const val STAGE_HOST_SHOWN = 1 shl 0
    const val STAGE_HOST_CASTING = 1 shl 1
    const val STAGE_HOST_FAINT = 1
    const val STAGE_HOST_OPAQUE = 2

    /** Whether a display with these STAGE_DISPLAY_* bits (and failures) can take the stage (core/viz/StageRun). */
    external fun stageDisplayEligible(flags: Int): Boolean

    /** The index of the connected display the stage shows on ([preferredId] while it qualifies), or -1. */
    external fun stagePickDisplay(
        ids: IntArray,
        flags: IntArray,
        preferredId: Int,
    ): Int

    /** True when the STAGE_PRESENT_* bits all hold: the stage moves to the picked display. */
    external fun stagePresents(flags: Int): Boolean

    const val STAGE_DISPLAY_PRESENTATION = 1 shl 0
    const val STAGE_DISPLAY_DEFAULT = 1 shl 1
    const val STAGE_DISPLAY_HOST = 1 shl 2
    const val STAGE_DISPLAY_ON = 1 shl 3
    const val STAGE_DISPLAY_FAILURE_SHIFT = 8
    const val STAGE_PRESENT_WANTED = 1 shl 0
    const val STAGE_PRESENT_ENTITLED = 1 shl 1
    const val STAGE_PRESENT_FOUND = 1 shl 2

    /**
     * What the stage's next/previous style steps (core/viz/StageStep): [page] is the open Visuals page
     * (STAGE_STEP_PAGE_*), [flags] the STAGE_STEP_* facts, [milkCount] the .milk presets listed and
     * [lookCount] the saved looks the user may apply. One of [STAGE_STEP_STYLES], [STAGE_STEP_LOOKS], [STAGE_STEP_MILK].
     */
    external fun stageStepTarget(
        page: Int,
        flags: Int,
        milkCount: Int,
        lookCount: Int,
    ): Int

    /** The item [step] places from [here] among [open]'s, wrapping, skipping [here] and the closed ones; -1 for none. */
    external fun stageStepIndex(
        open: BooleanArray,
        here: Int,
        step: Int,
    ): Int

    /**
     * The index in [paths] (the MilkDrop page's order) of the .milk [step] loads from [current] ("" when MilkDrop
     * is not on screen), previous retracing the steps taken; -1 for none.
     */
    external fun stageStepMilk(
        paths: Array<String>,
        current: String,
        step: Int,
    ): Int

    const val STAGE_STEP_PAGE_STYLES = 0
    const val STAGE_STEP_PAGE_CUSTOMIZE = 1
    const val STAGE_STEP_PAGE_PRESETS = 2
    const val STAGE_STEP_PAGE_MILKDROP = 3
    const val STAGE_STEP_PAGE_TEXTURES = 4
    const val STAGE_STEP_PAGE_SETTINGS = 5
    const val STAGE_STEP_SHEET_SHOWN = 1 shl 0
    const val STAGE_STEP_ON_MILK = 1 shl 1
    const val STAGE_STEP_ON_LOOK = 1 shl 2
    const val STAGE_STEP_MILK_OPEN = 1 shl 3
    const val STAGE_STEP_STYLES = 0
    const val STAGE_STEP_LOOKS = 1
    const val STAGE_STEP_MILK = 2

    // The live wallpaper's and the dream's run rule (core/viz/SurfaceRun). [flags] are the
    // SURFACE_* bits the shell observed; [animateUntilMs] is the wall-clock time the entitlement
    // lets the visual move until ([SURFACE_FOREVER] for Pro, a trial's end, 0 for free).

    /** 0 stopped, 1 a still frame, 2 moving, 3 dark (no visual). */
    external fun surfaceRunMode(
        flags: Int,
        animateUntilMs: Long,
        nowMs: Long,
    ): Int

    /** Why it is not moving: 0 moving, 1 hidden, 2 the entitlement, 3 reduced motion, 4 battery saver, 5 silence. */
    external fun surfaceReason(
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

    /**
     * Auto's style timer ([AUTO_TIMER_PLAYLIST], [AUTO_TIMER_RANDOM]; native `core/viz/AutoTimer`): a look
     * just went up at [nowMs], so the wait starts over with a new random spread on top of the interval.
     */
    external fun autoTimerRestart(
        timer: Int,
        nowMs: Long,
    )

    /**
     * Whether [timer]'s next look is due: the interval plus this look's spread has passed. True restarts
     * the wait. [running] false (off, paused, nothing to step to) is never due.
     */
    external fun autoTimerDue(
        timer: Int,
        nowMs: Long,
        intervalSec: Int,
        running: Boolean,
    ): Boolean

    /**
     * Auto is about to bring [key] on (a style id or a .milk path): the switch to it plays Auto's longer
     * fade (native `core/viz/TransitionChoice`).
     */
    external fun autoMarkChange(key: String)

    /** Auto's random draw (core/viz/AutoDraw): an index in 0 until [count], never [onScreen] while count > 1; -1 for none. */
    external fun autoPick(
        count: Int,
        onScreen: Int,
    ): Int

    /**
     * Visuals › Styles' Random (core/viz/StylePick): the index of the style to load among the page's styles,
     * [family] each one's family index and [open] whether the entitlement keeps it; never [onScreen], every family
     * or with [familyOnly] the on-screen style's family alone, a kept style while there is one; -1 for none.
     * With [familyOnly] in a family of one style, [STYLES_ROLL_INSIDE] when that style has a variation to roll
     * ([innerCount], the .milk presets for MilkDrop, [innerOnScreen] the one showing or -1).
     */
    external fun stylesRandom(
        family: IntArray,
        open: BooleanArray,
        onScreen: Int,
        familyOnly: Boolean,
        innerCount: Int,
        innerOnScreen: Int,
    ): Int

    const val STYLES_ROLL_INSIDE = -2

    /** The order a Styles list shows its [count] items: [lead] (what is on screen) first, the rest in order. */
    external fun stylesLead(
        count: Int,
        lead: Int,
    ): IntArray

    const val AUTO_TIMER_PLAYLIST = 0
    const val AUTO_TIMER_RANDOM = 1

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

    /** The dream: a silent rest shows no visual rather than a still frame. */
    const val SURFACE_DARK_WHEN_SILENT = 1 shl 9
    const val SURFACE_FOREVER = Long.MAX_VALUE

    /** The wallpaper's and the dream's fps before the user picks one (GEODE_SURFACE_DEFAULT_FPS). */
    const val SURFACE_DEFAULT_FPS = 30

    // The frame pacer (core/viz/frame/Pacer): which vsyncs a surface draws on, and the dt.

    /** Nine longs the caller owns, zeroed at first, stepped in place: the pacer's whole state. */
    const val PACER_STATE_LONGS = 9

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

    // AIFF files (core/audio/ingest/AiffReader), for the soundtrack transcoder.

    /**
     * An AIFF or AIFF-C (NONE, sowt) read as 16-bit PCM from [fd], which native takes and closes, also when it
     * returns 0 (not an AIFF it reads). [length] >= 0 reads that many bytes from [startOffset] on (an
     * AssetFileDescriptor's span); < 0 to the end. Freed with [aiffClose].
     */
    external fun aiffOpenFd(
        fd: Int,
        startOffset: Long,
        length: Long,
    ): Long

    external fun aiffClose(handle: Long)

    /** [out] gets the channel count, the sample rate and the COMM frame count (0 = read to the end). */
    external fun aiffInfo(
        handle: Long,
        out: LongArray,
    )

    /** Frames read over the total, 0..1. */
    external fun aiffProgress(handle: Long): Float

    /** Up to [out]'s size in interleaved 16-bit samples, whole frames while the file has them; -1 at the end. */
    external fun aiffReadS16(
        handle: Long,
        out: ShortArray,
    ): Int

    /** The feed's held frames up to its target into renderer [handle]'s sound engine; true once reached. */
    external fun vizHearTrack(
        handle: Long,
        feed: Long,
    ): Boolean

    /** Ends the pre-roll before frame 0; false when renderer [handle]'s engine heard nothing (the render must fail). */
    external fun vizEndPreRoll(
        handle: Long,
        rangeStartMs: Long,
    ): Boolean

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

    /**
     * Layers the ring at [layer] (0 for none) over the one at [handle]: every renderer fed [handle] hears
     * the two mixed natively, and [layer] alone while [handle] gains nothing. The mic over the player.
     */
    external fun pcmRingSetLayer(
        handle: Long,
        layer: Long,
    )

    /**
     * Marks the ring's writes as a capture's snapshots (the output mix's Visualizer), not a stream: each
     * write is then stitched natively onto the capture clock. Writer thread, before its first write. False
     * only when it could not allocate.
     */
    external fun pcmRingSetSnapshots(
        handle: Long,
        on: Boolean,
    ): Boolean

    /** A new stream: every reader restarts at its next take. Any thread. */
    external fun pcmRingBeginEpoch(handle: Long)

    /** Frames written since the last [pcmRingBeginEpoch]. */
    external fun pcmRingWritten(handle: Long): Long

    /** Frames ever written, across epochs: the position [playoutBufferHandled] counts ring samples in. */
    external fun pcmRingHead(handle: Long): Long

    /**
     * The playout clock ([playoutCreate]) of the sink that plays this ring, 0 for none: a renderer fed the
     * ring then hears up to the sample the speaker plays, not the newest. The clock must outlive the ring.
     */
    external fun pcmRingSetPlayout(
        handle: Long,
        playout: Long,
    )

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

    /** The user's A/V offset, clamped to ±[playoutMaxOffsetMs]; positive shows the picture later. */
    external fun playoutSetOffsetMs(
        handle: Long,
        ms: Int,
    )

    /** The offset's bound in ms, for Settings' Picture delay slider. */
    external fun playoutMaxOffsetMs(): Int

    /** The newest ring sample heard at [nowNs], never past the newest written; -1 with no map since a flush. */
    external fun playoutSpeakerFrame(
        handle: Long,
        nowNs: Long,
    ): Long

    /** Cast: while [on], the TV plays the ring as written and every reader takes the newest sample again. */
    external fun playoutSetBypass(
        handle: Long,
        on: Boolean,
    )

    /** Wall ms from [nowNs] until the newest sample the sink took is heard; 0 with no map, 2000 at most. */
    external fun playoutLagMs(
        handle: Long,
        nowNs: Long,
    ): Int

    /** The player's ring: its capacity and runway, sized natively so a reader at the speaker is not overrun. */
    external fun playoutRingFrames(): Int

    external fun playoutRingRunway(): Int

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

    // The process's one thermal state (core/viz/frame/Thermal), which reads the platform itself.

    /** The Visuals stage's "Phone is warm" note: the fps to name, once per process, when heat first drops the stage's rate; else 0. */
    external fun thermalTakeWarmNote(
        capFps: Float,
        pacedFps: Float,
    ): Int

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

    /** Size of the encoder feed's state (GEODE_CAST_AUDIO_FEED_STATE): frames taken or dropped, pending, rate. */
    const val CAST_AUDIO_FEED_STATE = 3

    /** Starts the AAC encoder's backlog [state] at [rateHz]; returns its cap in frames (a second). */
    external fun castAudioFeedStart(
        state: LongArray,
        rateHz: Int,
    ): Int

    /** A block of [frames] joins the backlog; returns how many of the oldest pending frames to drop first. */
    external fun castAudioFeedAdd(
        state: LongArray,
        frames: Int,
    ): Int

    /** Frames the next codec input buffer takes from the backlog's head (up to [bufferFrames]; 0 = none), its PTS in [ptsUs]`[0]`. */
    external fun castAudioFeedTake(
        state: LongArray,
        bufferFrames: Int,
        ptsUs: LongArray,
    ): Int

    // Show on a computer (core/viz/frame/LinkShare, LinkPage): the browser link's routes, heads, page, master
    // playlist and state; and its QR code (core/util/QrCode). cast/LiveHttpServer only moves the bytes.

    /** Resources [linkRoute] names (GEODE_LINK_*). */
    const val LINK_PAGE = 1
    const val LINK_SCRIPT = 2
    const val LINK_STYLE = 3
    const val LINK_MASTER = 4
    const val LINK_PLAYLIST = 5
    const val LINK_INIT = 6
    const val LINK_SEGMENT = 7

    /** stream.mp4: the browser's live feed, the init segment then each fragment as it is cut, held open. */
    const val LINK_STREAM = 9

    /** Browsers fed at once (GEODE_LINK_MAX_STREAMS); past it the feed answers 503 and the page retries. */
    const val LINK_MAX_STREAMS = 3

    /** rtc: the page POSTs its WebRTC offer here; the answer is the body of the 200. */
    const val LINK_RTC = 10

    /** [linkBodyLength]'s answers besides a length: no body announced, or one refused. */
    const val LINK_NO_BODY = -1L
    const val LINK_BAD_BODY = -2L

    /** The body a request's [headers] (each line without its break) announce: its length, or LINK_NO_BODY / LINK_BAD_BODY. */
    external fun linkBodyLength(
        headers: Array<String>,
        max: Long,
    ): Long

    /** The HTTP status for the rtc POST from [webrtcAnswer]'s result: 200, 503 (busy) or 400. */
    external fun linkRtcStatus(answerResult: Int): Int

    /**
     * The link the user sees, copies and shares: the page's address with its IPv4 host ([address], host order)
     * written as one decimal number, not the dotted address; null when it cannot be made.
     */
    external fun linkShareUrl(
        address: Int,
        port: Int,
        token: String,
    ): String?

    /** A short friendly name for [token] ("Blue Comet"), shown beside the QR code. */
    external fun linkLabel(token: String): String

    /**
     * Routes a request's [method] and [target] against [token]: returns the HTTP status and fills [out] (2 longs)
     * with the resource and, for a segment, its sequence.
     */
    external fun linkRoute(
        method: String,
        target: String,
        token: String,
        out: LongArray,
    ): Int

    /** The response head (status line, headers, blank line) for [status] and [resource] with a [length]-byte body. */
    external fun linkResponseHead(
        status: Int,
        resource: Int,
        length: Long,
        token: String,
    ): String

    /** A static file of the page ([LINK_PAGE], [LINK_SCRIPT] or [LINK_STYLE]); null for any other resource. */
    external fun linkAsset(resource: Int): ByteArray?

    /** The master playlist, CODECS read from the encoder's [init] segment; null until they can be read. */
    external fun linkMasterPlaylist(init: ByteArray): String?

    /** Size of the share's state (GEODE_LINK_STATE): phase, reason. */
    const val LINK_STATE = 2
    const val LINK_PHASE_IDLE = 0
    const val LINK_PHASE_STARTING = 1
    const val LINK_PHASE_LIVE = 2
    const val LINK_PHASE_ENDED = 3
    const val LINK_REASON_STREAM = 1
    const val LINK_REASON_NO_LAN = 2
    const val LINK_REASON_BACKGROUND = 3
    const val LINK_REASON_NETWORK = 4
    const val LINK_REASON_TRIAL = 5
    const val LINK_EVENT_START = 1
    const val LINK_EVENT_READY = 2
    const val LINK_EVENT_FAILED = 3
    const val LINK_EVENT_STOP = 4
    const val LINK_EVENT_SHOWN = 5
    const val LINK_ENTITLED = 1
    const val LINK_HAS_LAN = 2
    const val LINK_DO_START_STREAM = 1
    const val LINK_DO_STOP_STREAM = 2
    const val LINK_DO_PAYWALL = 4

    /** One event of the share's state machine over [state] ([LINK_STATE] ints); returns LINK_DO_* bits. */
    external fun linkStep(
        state: IntArray,
        event: Int,
        arg: Int,
    ): Int

    /** [linkAdmit]'s wants (GEODE_LINK_WANT_*) and answers (GEODE_LINK_PEER_*). */
    const val LINK_WANT_TV = 1
    const val LINK_WANT_LINK = 2
    const val LINK_PEER_REFUSED = 0
    const val LINK_PEER_RECEIVER = 1
    const val LINK_PEER_BROWSER = 2

    /**
     * Whether a connection from [peer] is let in (IPv4 as an int, host order, 0 = unknown), for a server bound to
     * [bound]/[prefix] with the Cast device at [tv] and [wants] LINK_WANT_* bits; returns LINK_PEER_*.
     */
    external fun linkAdmit(
        peer: Int,
        bound: Int,
        prefix: Int,
        tv: Int,
        wants: Int,
    ): Int

    /** [linkReceiverHealth]'s answers (GEODE_LINK_RECEIVER_*). */
    const val LINK_RECEIVER_OK = 0
    const val LINK_RECEIVER_ERROR = 1
    const val LINK_RECEIVER_GONE = 2
    const val LINK_RECEIVER_UNREACHABLE = 3

    /**
     * Whether the TV plays the stream: the LOAD's and the TV's last fetch's times ([nowMs]'s clock, 0 = never)
     * and the receiver's MediaStatus [playerState] and [idleReason]; returns LINK_RECEIVER_*.
     */
    external fun linkReceiverHealth(
        nowMs: Long,
        loadMs: Long,
        lastFetchMs: Long,
        playerState: Int,
        idleReason: Int,
    ): Int

    /** Modules a QR code can have (GEODE_QR_MAX_MODULES), the size of [qrEncode]'s buffer. */
    const val QR_MAX_MODULES = 57 * 57

    /** [text]'s QR code (byte mode, level M) into [out], row-major, 1 = dark; returns its side, 0 when too long. */
    external fun qrEncode(
        text: String,
        out: ByteArray,
    ): Int

    // Show on a computer over WebRTC (core/cast/WebRtcLink, rules in core/viz/frame/WebRtcRule): the browser's
    // low-delay path, its media encrypted with DTLS-SRTP. One handle per cast stream; 0 = none.

    /** Largest offer [webrtcAnswer] takes (GEODE_WEBRTC_OFFER_MAX). */
    const val WEBRTC_OFFER_MAX = 16 * 1024
    const val WEBRTC_REFUSED_FULL = -4
    const val WEBRTC_REFUSED_NOT_READY = -5

    /** Opens the link bound to the server's address: [boundDotted], and [bound]/[prefix] as [linkAdmit] reads them. */
    external fun webrtcOpen(
        boundDotted: String,
        bound: Int,
        prefix: Int,
    ): Long

    external fun webrtcClose(handle: Long)

    /** The answer to a browser's [offer], or null; [result] (1 int) gets its length or a WEBRTC_REFUSED_* code. */
    external fun webrtcAnswer(
        handle: Long,
        offer: String,
        result: IntArray,
    ): String?

    /** One encoded H.264 frame (the first [size] bytes of [data], Annex B) as the cast encoder wrote it. */
    external fun webrtcVideo(
        handle: Long,
        data: ByteArray,
        size: Int,
        ptsUs: Long,
        key: Boolean,
    )

    /** The H.264 codec config (SPS and PPS) the encoder hands over before its first frame. */
    external fun webrtcVideoConfig(
        handle: Long,
        data: ByteArray,
        size: Int,
    )

    /** One Opus packet (48 kHz stereo) and its PTS. */
    external fun webrtcAudio(
        handle: Long,
        data: ByteArray,
        size: Int,
        ptsUs: Long,
    )

    /** Browsers connected over WebRTC now. */
    external fun webrtcPeers(handle: Long): Int

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

    /** Size of the decode times [hlsRewriteFragment] keeps (GEODE_HLS_TRACKS), zeroed at the stream's start. */
    const val HLS_TRACKS = 8

    /** [fragment] (moof + mdat, its moof at [moofAt] in the muxer's output) as a self-contained segment; null = refused. */
    external fun hlsRewriteFragment(
        init: ByteArray,
        moofAt: Long,
        fragment: ByteArray,
        decodeTimes: LongArray,
    ): ByteArray?
}
