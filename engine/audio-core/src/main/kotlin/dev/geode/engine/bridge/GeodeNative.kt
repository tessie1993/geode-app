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

    /** Whether the chain runs a format at all: [encoding] (`C.ENCODING_PCM_*`) 16-bit or float, 1 or 2 channels. */
    external fun dspRuns(
        encoding: Int,
        channels: Int,
    ): Boolean

    /** Makes the staged chain live, then clears its filter state. */
    external fun dspFlush(handle: Long)

    external fun dspDestroy(handle: Long)

    external fun dspBandCenterHz(band: Int): Float

    /** How many built-in equalizer curves there are. */
    external fun dspEqPresetCount(): Int

    /** Curve [preset]'s band levels in millibels into [out]; the bands written, 0 for no such curve. */
    external fun dspEqPresetLevels(
        preset: Int,
        out: IntArray,
    ): Int

    const val DSP_FX_BAND: Int = 0
    const val DSP_FX_BASS_BOOST: Int = 1
    const val DSP_FX_LOUDNESS: Int = 2
    const val BOOKMARK_IGNORE: Int = 0
    const val BOOKMARK_CLEAR: Int = 1
    const val BOOKMARK_SAVE: Int = 2

    /** The equalizer's bands. */
    external fun dspBandCount(): Int

    /** A sound effect's range ([DSP_FX_BAND], [DSP_FX_BASS_BOOST], [DSP_FX_LOUDNESS]) and a value held to it. */
    external fun dspFxMin(control: Int): Int

    external fun dspFxMax(control: Int): Int

    external fun dspFxClamp(
        control: Int,
        value: Int,
    ): Int

    /**
     * The band levels a session starts on into [out]: saved curve [preset]'s when it names one, else the saved
     * text's ([csv]: millibels, comma separated, low band first; null for none), a band with neither at 0, each
     * in range. An entry that is not a whole number is dropped and the rest move up. The bands written.
     */
    external fun dspRestoreBandsText(
        preset: Int,
        csv: String?,
        out: IntArray,
    ): Int

    /** The saved text for [levels]: millibels, comma separated. */
    external fun dspEncodeBands(levels: IntArray): String

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

    /** Settings › Playback's pitch row (audio/dsp/PlaybackControls): the rate a shift of [semitones] plays at. */
    external fun playbackPitchRatio(semitones: Float): Float

    /** The saved playback options that have a range (`audio/dsp/PlaybackControls`), in `geode_playback_range` order. */
    const val PLAYBACK_FADE_MS = 0
    const val PLAYBACK_CROSSFADE_MS = 1
    const val PLAYBACK_REPEAT_MODE = 2
    const val PLAYBACK_SPEED = 3
    const val PLAYBACK_PITCH_SEMITONES = 4
    const val PLAYBACK_REPLAYGAIN_MODE = 5
    const val PLAYBACK_REPLAYGAIN_PREAMP_DB = 6
    const val PLAYBACK_CONTROLS = 7

    /** Fields of the float[4] [playbackRange] returns. */
    const val PLAYBACK_RANGE_MIN = 0
    const val PLAYBACK_RANGE_MAX = 1
    const val PLAYBACK_RANGE_STEP = 2
    const val PLAYBACK_RANGE_DEFAULT = 3

    /** Min, max, step (0: none) and default of playback option [control]; null for one that is not one. */
    external fun playbackRange(control: Int): FloatArray?

    /** [value] held to the option's range, a NaN to its default. */
    external fun playbackClamp(
        control: Int,
        value: Float,
    ): Float

    /** A crossfade slider position in seconds as ms: whole seconds, Off below half a second, held to the range. */
    external fun playbackCrossfadeMs(seconds: Float): Int

    /** A Settings › Playback slider's [value] on its [step]: the nearest multiple, a half rounding up. */
    external fun playbackSnap(
        value: Float,
        step: Float,
    ): Float

    /** What a track that will not play leads to (audio/dsp/PlaybackControls): 0 skip, 1 stop at the end, 2 stop after 3 failures. */
    external fun playbackErrorAction(
        consecutiveFailures: Int,
        hasNext: Boolean,
    ): Int

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

    /**
     * The global search sheet (core/library/Search): [tracks] holds five UTF-8 cells per track (title, artist,
     * album, one more field, and the URI every copy of a song shares), [preferred] marks the copy shown when
     * copies meet, [playlists] and [presets] are names. Returns three counts (tracks, playlists, presets), then
     * those rows, best first. A blank [query] finds nothing.
     */
    external fun searchSheet(
        tracks: Array<ByteArray?>,
        preferred: BooleanArray,
        playlists: Array<ByteArray?>,
        presets: Array<ByteArray?>,
        query: ByteArray,
    ): IntArray

    /**
     * A playlist file the user picked (core/library/Playlist, `geode_playlist_parse`): its name and bytes in,
     * the text out. Record 0 is `O`, name or `E`, reason; then one record per entry: location, title, duration
     * in ms (-1 unknown) and the file name inside the location. Fields are split by U+001F, records by U+001E.
     */
    external fun playlistParse(
        fileName: ByteArray,
        data: ByteArray,
    ): ByteArray

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

    /** Process-wide (core/viz/LayerState): every renderer draws it; "" = no layer; [blendMode] indexes [layerBlendNames]. */
    external fun layerSet(
        sceneId: String,
        mix: Float,
        blendMode: Int,
    )

    /** The blends a layer takes (core/viz/VisualSafety), in [layerSet]'s order, each label followed by a newline. */
    external fun layerBlendNames(): String

    /** Screen, the blend a new layer starts on (`GEODE_LAYER_BLEND_DEFAULT`). */
    const val LAYER_BLEND_DEFAULT: Int = 1

    external fun vizSetTransition(
        handle: Long,
        id: String,
        durationMs: Long,
    )

    external fun vizLoadMilkPreset(
        handle: Long,
        path: String,
    )

    external fun vizSetMilkTextureDir(
        handle: Long,
        dir: String,
    )

    external fun vizTakeMilkPresetLoaded(handle: Long): String?

    // The MilkDrop preset every surface restores (core/viz/MilkMemory), one per process. The shell seeds it
    // with the path it saved and saves again when [milkMemoryLoaded] or [milkMemoryForget] says so.

    /** The saved path, null for none; only a process's first seed counts. */
    external fun milkMemorySeed(saved: String?)

    /** The UI asked for [path]; any thread. */
    external fun milkMemoryRequest(path: String)

    /** A renderer reported [path] shown; true when it is new and must be saved. */
    external fun milkMemoryLoaded(path: String): Boolean

    /** [paths] were deleted; true when the saved path must be removed. */
    external fun milkMemoryForget(paths: Array<String>): Boolean

    /** The last preset shown, still on disk; null when none has been. */
    external fun milkMemoryLastShown(): String?

    /**
     * What a new renderer starts on; null only when not even a bundled preset is there. With nothing shown or
     * asked for, the starter pack goes into [milkDir] first. [assets] is the app's `AssetManager`, typed as Any like [vizCreate]'s.
     */
    external fun milkMemoryRestorePath(
        assets: Any,
        milkDir: String,
    ): String?

    /** Copies the bundled presets into [milkDir] once per pack version, never over a file; how many it wrote. */
    external fun milkStarterInstall(
        assets: Any,
        milkDir: String,
    ): Int

    external fun vizTakeDirectorStyleMoments(handle: Long): Int

    external fun vizWriteAudio(
        handle: Long,
        interleaved: FloatArray,
        frames: Int,
        channels: Int,
        sampleRate: Int,
    )

    /** Settings › Audio's saved tuning (null: the engine's defaults), resolved natively as [soundTuningResolve]. */
    external fun vizSetSoundTuning(
        handle: Long,
        saved: FloatArray?,
    )

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
     * over every style; never a colour, never a route. [on] is the user's switch as set, which an export directs by (pinned natively);
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
     * so the live note response differs every time; an export pins its settings with none,
     * so exporting the same play twice draws the same picture, though not the one
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

    /** Bit per Music action whose target reaches [styleId] (geode_music_actions_for). */
    external fun musicActionsFor(styleId: String): Int

    /** Mirrors GEODE_HAND_UNCLAIMED. */
    const val HAND_UNCLAIMED: Int = 0x40000000

    /** Mirrors GEODE_HAND_ALL: every moment and every unclaimed source, the whole list the user's. */
    const val HAND_ALL: Int = 0x400003FF

    // The user's modulation (core/viz/ModDocument, api/geode_modulation_api.h): the waves, the envelopes and
    // the reaction routes. The shell holds each as the text it saves; native reads, edits and plays them.

    /** What the editors show (geode_mod_catalog_text): every name, label, range and count, as a line table. */
    external fun modCatalogText(): String

    /** The saved waves as native reads them: every slot and field, each in range. Null or broken text: the defaults. */
    external fun modLfosRead(text: String?): String

    /** The saved envelopes, likewise. */
    external fun modAdsrsRead(text: String?): String

    /** The user's saved routes as native reads them; null for text that is no route list (Auto). */
    external fun modReactionsRead(text: String?): String?

    /** One wave slot: its [MOD_LFO_ENABLED]..[MOD_LFO_MOMENT] fields, then 1 while its target moves [styleId]. */
    external fun modLfoValues(
        lfos: String,
        slot: Int,
        styleId: String,
    ): FloatArray

    /** [lfos] with one [field] of one [slot] set (geode_mod_lfo_edit); enums go as their catalog ordinal. */
    external fun modLfoEdit(
        lfos: String,
        slot: Int,
        field: Int,
        value: Float,
    ): String

    /** One envelope: its [MOD_ADSR_ENABLED]..[MOD_ADSR_MOMENT] fields, then the ordinals of the targets it moves. */
    external fun modAdsrValues(
        adsrs: String,
        slot: Int,
    ): FloatArray

    /** [adsrs] with one [field] of one [slot] set; [MOD_ADSR_TARGET] adds or takes away the target [value] names. */
    external fun modAdsrEdit(
        adsrs: String,
        slot: Int,
        field: Int,
        value: Float,
    ): String

    /** Which of the wave [slot]'s controls do anything as it stands (geode_mod_lfo_shows): [MOD_SHOWS_WAVE] and the rest. */
    external fun modLfoShows(
        lfos: String,
        slot: Int,
    ): Int

    /** Which of the envelope [slot]'s controls do anything as it stands (geode_mod_adsr_shows). */
    external fun modAdsrShows(
        adsrs: String,
        slot: Int,
    ): Int

    // Mirrors of GEODE_MOD_SHOWS_*: the bits [modLfoShows] and [modAdsrShows] return.
    const val MOD_SHOWS_WAVE: Int = 1
    const val MOD_SHOWS_RATE: Int = 2
    const val MOD_SHOWS_KNOB: Int = 4

    /** The target ordinals a [picker] (a wave slot, or [MOD_PICKER_REACTION]) lists on [styleId]. */
    external fun modTargetsOffered(
        styleId: String,
        picker: Int,
    ): IntArray

    /** The target ordinals an envelope's picker lists on [styleId]. */
    external fun modAdsrTargetsOffered(
        adsrs: String,
        slot: Int,
        styleId: String,
    ): IntArray

    /**
     * What plays on [styleId] (geode_mod_routes_playing): the user's [reactions] (null: Auto) on the hand
     * [mask]'s sources, the style's own on the rest. [MOD_ROUTE_VALUES] per route.
     */
    external fun modRoutesPlaying(
        styleId: String,
        seed: Int,
        reactions: String?,
        mask: Int,
    ): FloatArray

    /**
     * One edit of the reactions (geode_mod_reactions_edit, [op] a `MOD_EDIT_*`). Returns the user's new routes
     * and writes the mask to save beside them to `outMask[0]`; null (and mask 0) when every route is Auto again.
     */
    external fun modReactionsEdit(
        styleId: String,
        seed: Int,
        reactions: String?,
        mask: Int,
        op: Int,
        a: Int,
        b: Int,
        value: Float,
        outMask: IntArray,
    ): String?

    /** Customize › Music's rows, [MOD_MUSIC_VALUES] per moment: mine, route count, strength, on bits, fits bits. */
    external fun modMusicState(
        styleId: String,
        seed: Int,
        reactions: String?,
        mask: Int,
    ): FloatArray

    /** [MOD_RECIPE_ON], [MOD_RECIPE_FITS] and [MOD_RECIPE_OFFERED] for [recipe] on [styleId] over what plays. */
    external fun modRecipeState(
        recipe: Int,
        styleId: String,
        seed: Int,
        reactions: String?,
        mask: Int,
    ): Int

    /**
     * Randomize for the waves (core/viz/WaveRandomizer): [lfos] rolled on the look in [params]. [runningOnly]
     * false is the wave editor's button (every wave on and rolled); true is a category roll, which only re-rolls
     * the steady waves already on and none when [locked] keeps them. Null when it changed nothing.
     */
    external fun modWavesRandomize(
        styleId: String?,
        locked: String?,
        seed: Long,
        runningOnly: Boolean,
        params: FloatArray,
        lfos: String,
    ): String?

    /** A renderer's waves, as the saved text (geode_viz_set_lfos). */
    external fun vizSetLfos(
        handle: Long,
        lfos: String,
    )

    /** A renderer's envelopes, as the saved text (geode_viz_set_adsrs). */
    external fun vizSetAdsrs(
        handle: Long,
        adsrs: String,
    )

    /**
     * A renderer's reactions (geode_viz_set_reactions): the user's saved routes (null: Auto, the style's own
     * play). Native resolves what plays from these, the scene, `reactSeed` and the hand mask ([handSet], or an
     * export's pin), which it reads itself. Any thread.
     */
    external fun vizSetReactions(
        handle: Long,
        reactions: String?,
    )

    // Mirrors of GEODE_MOD_LFO_*: a wave's fields.
    const val MOD_LFO_ENABLED: Int = 0
    const val MOD_LFO_SOURCE: Int = 1
    const val MOD_LFO_TARGET: Int = 2
    const val MOD_LFO_WAVE: Int = 3
    const val MOD_LFO_RATE_SECONDS: Int = 4
    const val MOD_LFO_DEPTH: Int = 5
    const val MOD_LFO_POLARITY: Int = 6
    const val MOD_LFO_CURVE: Int = 7
    const val MOD_LFO_BEATS: Int = 8
    const val MOD_LFO_KNOB: Int = 9
    const val MOD_LFO_MOMENT: Int = 10

    /** Where [modLfoValues] says whether the slot's target moves the style. */
    const val MOD_LFO_REACHES: Int = 11

    // Mirrors of GEODE_MOD_ADSR_*: an envelope's fields; TARGET is edit-only.
    const val MOD_ADSR_ENABLED: Int = 0
    const val MOD_ADSR_ATTACK: Int = 1
    const val MOD_ADSR_DECAY: Int = 2
    const val MOD_ADSR_SUSTAIN: Int = 3
    const val MOD_ADSR_RELEASE: Int = 4
    const val MOD_ADSR_AMOUNT: Int = 5
    const val MOD_ADSR_BAND: Int = 6
    const val MOD_ADSR_GATE_THRESHOLD: Int = 7
    const val MOD_ADSR_SUSTAIN_TRACK: Int = 8
    const val MOD_ADSR_RETRIGGER: Int = 9
    const val MOD_ADSR_KNOB: Int = 10
    const val MOD_ADSR_MOMENT: Int = 11
    const val MOD_ADSR_TARGET: Int = 12

    /** Mirrors GEODE_MOD_ADSR_VALUES: the fields [modAdsrValues] writes before the target ordinals. */
    const val MOD_ADSR_VALUES: Int = 12

    // Mirrors of GEODE_MOD_ROUTE_*: a route's fields.
    const val MOD_ROUTE_ENABLED: Int = 0
    const val MOD_ROUTE_SOURCE: Int = 1
    const val MOD_ROUTE_TARGET: Int = 2
    const val MOD_ROUTE_MODE: Int = 3
    const val MOD_ROUTE_AMOUNT: Int = 4
    const val MOD_ROUTE_CURVE: Int = 5
    const val MOD_ROUTE_ATTACK_SECONDS: Int = 6
    const val MOD_ROUTE_RELEASE_SECONDS: Int = 7

    /** Where a [modRoutesPlaying] route says whether its target moves the style. */
    const val MOD_ROUTE_REACHES: Int = 8

    /** Mirrors GEODE_MOD_ROUTE_VALUES. */
    const val MOD_ROUTE_VALUES: Int = 9

    // Mirrors of GEODE_MOD_EDIT_*: [modReactionsEdit]'s op.
    const val MOD_EDIT_ROUTE_SET: Int = 0
    const val MOD_EDIT_ROUTE_ADD: Int = 1
    const val MOD_EDIT_ROUTE_REMOVE: Int = 2
    const val MOD_EDIT_RECIPE_TOGGLE: Int = 3
    const val MOD_EDIT_MUSIC_TOGGLE: Int = 4
    const val MOD_EDIT_MUSIC_STRENGTH: Int = 5
    const val MOD_EDIT_MUSIC_RESET: Int = 6
    const val MOD_EDIT_CLEAR: Int = 7

    /** Mirrors GEODE_MOD_RECIPE_ON: every route the recipe would add to the style already plays. */
    const val MOD_RECIPE_ON: Int = 1

    /** Mirrors GEODE_MOD_RECIPE_FITS: turning the recipe on stays within the matrix. */
    const val MOD_RECIPE_FITS: Int = 2

    /** Mirrors GEODE_MOD_RECIPE_OFFERED: the recipe has a route the style can show. */
    const val MOD_RECIPE_OFFERED: Int = 4

    /** Mirrors GEODE_MOD_MUSIC_VALUES. */
    const val MOD_MUSIC_VALUES: Int = 5

    /** Mirrors GEODE_MOD_PICKER_REACTION: the route editor's target picker. */
    const val MOD_PICKER_REACTION: Int = -2

    /** The style a live surface draws, latched: each [vizFrame] asks for it until the renderer can draw it. Any thread. */
    external fun vizRequestScene(
        handle: Long,
        sceneId: String,
    )

    /**
     * The user's whole look (geode_viz_set_look): native passes on only the parts that differ from the look set last
     * on this handle, all of them on a new one. [reactions] null means the style's own routes. Any thread.
     */
    external fun vizSetLook(
        handle: Long,
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

    /** The look a renderer starts on before one is set: scene id, transition id and its ms, a line each. */
    external fun vizLookDefaults(): String

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

    /** Mirrors GEODE_FRAME_LIQUID_CHANGED: Liquid mode changed; read it with [stageLiquidMode]. */
    const val FRAME_LIQUID_CHANGED: Int = 0x4

    /** Mirrors GEODE_FRAME_ERROR_CHANGED: the error text changed; read it with [vizLastError]. */
    const val FRAME_ERROR_CHANGED: Int = 0x8

    /** Mirrors GEODE_FRAME_MILK_LOADED: MilkDrop finished loading a preset; take it with [vizTakeMilkPresetLoaded]. */
    const val FRAME_MILK_LOADED: Int = 0x10

    /** Mirrors GEODE_FRAME_DIRECTOR_MOMENT: take the moments with [vizTakeDirectorStyleMoments]. */
    const val FRAME_DIRECTOR_MOMENT: Int = 0x20

    /** Mirrors GEODE_FRAME_STILL_READY: the still asked for is ready ([stageStillSize], [stageStillRead]). */
    const val FRAME_STILL_READY: Int = 0x40

    external fun vizSetOffscreen(
        handle: Long,
        on: Boolean,
    )

    /**
     * The live wallpaper's on-screen window (`core/viz/frame/WallpaperWindow.hpp`): from the surface and
     * display sizes and the raw `onOffsetsChanged` values (xOffset, yOffset, xOffsetStep, yOffsetStep,
     * xPixels, yPixels), writes left, top, width, height into [out] (4 floats), as [stageSetEdgeGlow] takes them.
     */
    external fun wallpaperWindow(
        surfaceW: Float,
        surfaceH: Float,
        displayW: Float,
        displayH: Float,
        offsets: FloatArray?,
        out: FloatArray,
    )

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

    external fun vizRender(
        handle: Long,
        timeSeconds: Double,
        targetFbo: Int,
    )

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
     * The scene and style catalogue (core/viz/Catalogue), one question per call (`geode_catalogue_*`). A
     * list comes back as its items each followed by a newline. The answers never change in a process, so
     * the shell asks once and keeps them (dev.geode.render.scene.StyleCatalogue).
     *
     * [catalogueStyleIds]: the styles with a trait ([STYLE_ANY] and the rest), in picker order.
     */
    external fun catalogueStyleIds(trait: Int): String

    /** The Styles page's families, in order. */
    external fun cataloguePickerNames(): String

    /** One Styles page family's styles, in order; none for an unknown family. */
    external fun cataloguePickerIds(family: String): String

    /** The style a saved id opens on: a retired id's replacement, any other id itself. */
    external fun catalogueResolve(styleId: String): String

    /** The style a saved look of this one is filed under: Fluid's and Cymatics' looks share one each. */
    external fun cataloguePresetStyle(styleId: String): String

    /** The scope name of a Customize control (its key); `UNIVERSAL` for a control no scope names. */
    external fun catalogueScopeOf(paramKey: String): String

    /** Whether a scope, by its name, moves the style. */
    external fun catalogueReaches(
        scope: String,
        styleId: String,
    ): Boolean

    /** The value a style holds a control at; NaN when the control is the user's. */
    external fun catalogueOverride(
        styleId: String,
        paramKey: String,
    ): Float

    /** A size the controls quote: `fluidTiers`, `touchPoints`, `touchRotationPerDegree`; NaN for any other name. */
    external fun catalogueValue(name: String): Float

    /** The traits [catalogueStyleIds] takes (`GEODE_STYLE_*`). */
    const val STYLE_ANY: Int = 0
    const val STYLE_SHADER: Int = 1

    /** In a shader family and not the style that opens it. */
    const val STYLE_FAMILY_FOLLOWER: Int = 2

    /** What Auto's random step draws from. */
    const val STYLE_AUTO_DRAW: Int = 3
    const val STYLE_FLUID: Int = 4
    const val STYLE_CYMATICS: Int = 5

    /** The MilkDrop renderer's one style id, which the shell names to open the preset browser. */
    const val STYLE_MILKDROP: String = "milkdrop"

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
     * The text a Customize control shows for [value] (core/viz/ParamText, `geode_param_text`): a %, ×, s or
     * ms, Hz or °, and "Default" at [default] (NaN for a control with none). [unit] is a ParamUnit's ordinal;
     * -1 takes the control's own, by its [key] and then its range [lo]..[hi]. A range below zero signs a
     * percentage.
     */
    external fun paramText(
        key: String,
        unit: Int,
        value: Float,
        lo: Float,
        hi: Float,
        default: Float,
    ): String

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

    /** Customize's Kaleidoscope control: `shown|selected|flipped`, then the Folds labels (`geode_customize_kaleidoscope`). */
    external fun customizeKaleidoscope(
        on: Boolean,
        symmetry: Int,
    ): String

    /** The `symmetry` Folds menu [row] sets (`geode_params_kaleidoscope_pick`). Pure. */
    external fun paramsKaleidoscopePick(row: Int): Int

    /**
     * The scene params object of a preset, link, template look or saved live look (core/viz/ParamsJson): every live
     * row of the wire [frame] under its name as org.json wrote it, then the two palette slot ids, as compact JSON
     * (`geode_params_write_json`). Null for a short frame. Pure.
     */
    external fun paramsWriteJson(
        frame: FloatArray,
        customPaletteId: String,
        customPalette2Id: String,
    ): String?

    /**
     * A params object read as org.json's opt* read it, sanitized into [out] (a wire frame), and the two palette slot
     * ids into [ids] (two slots; "" when missing) (`geode_params_read_json`): the object at [path] (nested keys,
     * newline-separated, "" for the top) of [document], the text org.json parsed, when that is standard JSON, else
     * [fallback], the object as org.json printed it. False, nothing written, when neither holds an object. Pure.
     */
    external fun paramsReadJson(
        document: String?,
        path: String,
        fallback: String?,
        out: FloatArray,
        ids: Array<String?>,
    ): Boolean

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

    /** The shell's accent for a palette of hue [base] and [range]: a packed 0xRRGGBB (`geode_colour_palette_accent`). Pure. */
    external fun colourPaletteAccent(
        base: Float,
        range: Float,
    ): Int

    /** A cover's ARGB pixels as [base hue, span, confidence], or null when none is coloured (`geode_art_palette_extract`). Pure. */
    external fun artPaletteExtract(argb: IntArray): FloatArray?

    /**
     * The widget glow's three lights as 6 floats each: x and y (shares of the bitmap), radius (share of its width), hue in
     * degrees, saturation, value (`geode_widget_glow`). [styleHash] is the style id's `hashCode`. Pure.
     */
    external fun widgetGlow(
        artHue: Float,
        artSpan: Float,
        hasStyle: Boolean,
        styleHash: Int,
        tick: Int,
    ): FloatArray

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

    /** Rewrites [points] (x, y) px pairs of [xy] in a [width] x [height] view as y-up NDC, in place (core/viz/TouchGesture). */
    external fun touchToNdc(
        xy: FloatArray,
        points: Int,
        width: Float,
        height: Float,
    )

    /**
     * Two fingers at [start], [last] and [now], each (ax, ay, bx, by) px, into [out] (6 floats): pan x, pan y and
     * zoom since start, twist in radians since start, and the step since last as a scale factor and degrees.
     */
    external fun touchPair(
        start: FloatArray,
        last: FloatArray,
        now: FloatArray,
        out: FloatArray,
    )

    /** Touch play's stir: [step]'s two NDC points into [xy] (4 floats) and the ms to hold them; 0 once it is over. */
    external fun touchPulse(
        step: Int,
        xy: FloatArray,
    ): Int

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

    /** How many fields of the wire frame [input] differ from the defaults (core/viz/CustomizeMap `changedCount`). */
    external fun customizeChangedCount(input: FloatArray): Int

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

    /** The background blends [vizSetUnderlay] takes, 0 until this count: 0 screen, 1 multiply, 2 add. */
    external fun underlayBlendCount(): Int

    /** The name a blend is saved under; [underlayBlendOf] reads it back (an unknown or null name is 0). */
    external fun underlayBlendName(blend: Int): String

    external fun underlayBlendOf(name: String?): Int

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

    // A loop render (core/output/LoopRender, core/api/geode_loop_job_api.h): the encoder it settles on, each
    // drift stop's frames, seam and presentation times and the scratch file each stop goes into are native.
    // The shell fills the job in, runs it on a worker thread and polls it from another; native never calls back.

    /** A job to fill in, 0 when out of memory. Freed with [loopJobDestroy] once no call is in flight. */
    external fun loopJobCreate(): Long

    external fun loopJobDestroy(job: Long)

    /**
     * The frame, the bit rate asked for, the loop and its crossfade, how long the extended file will run
     * ([longFormMs] 0: unknown) and the directory the scratch files go in.
     */
    external fun loopJobSetOutput(
        job: Long,
        cacheDir: String,
        width: Int,
        height: Int,
        bitRate: Int,
        loopMs: Long,
        crossfadeMs: Long,
        fps: Int,
        longFormMs: Long,
    )

    /** The palette drift across the reel: [stops] loops, the last one [hueTurns] and [warmth] from the first. */
    external fun loopJobSetDrift(
        job: Long,
        hueTurns: Float,
        warmth: Float,
        stops: Int,
    )

    /** A codec to try, in the order added: its MIME type and the device's top frame rate for it (<= 0: unsaid). */
    external fun loopJobAddCodec(
        job: Long,
        mime: String,
        topFps: Int,
    )

    /** The track every stop hears: [fd] is native's from here on; [length] < 0 is the whole descriptor. */
    external fun loopJobSetTrack(
        job: Long,
        fd: Int,
        offset: Long,
        length: Long,
    )

    /** The renderer's inputs. [assets] is the AssetManager (typed as Any because this module has no Android SDK). */
    external fun loopJobSetScene(
        job: Long,
        assets: Any,
        milkTextureDir: String,
        sceneId: String,
        milkPath: String?,
        entitled: Boolean,
    )

    external fun loopJobSetParams(
        job: Long,
        values: FloatArray,
    )

    external fun loopJobSetLfos(
        job: Long,
        lfos: String,
    )

    external fun loopJobSetAdsrs(
        job: Long,
        adsrs: String,
    )

    /** [saved] null: the default tuning. */
    external fun loopJobSetSoundTuning(
        job: Long,
        saved: FloatArray?,
    )

    /** [reactions] null: Auto, the style's own play. */
    external fun loopJobSetReactions(
        job: Long,
        reactions: String?,
    )

    external fun loopJobSetMotion(
        job: Long,
        reducedMotion: Boolean,
        transitionId: String,
        transitionMs: Long,
    )

    external fun loopJobSetUnderlay(
        job: Long,
        pixels: IntArray?,
        width: Int,
        height: Int,
        blend: Int,
        amount: Float,
    )

    /** The wordmark's tile, a direct buffer of [tileWidth] * [tileHeight] * 4 bytes; false when it is not one. */
    external fun loopJobSetMark(
        job: Long,
        rgba: java.nio.ByteBuffer,
        tileWidth: Int,
        tileHeight: Int,
        placement: Int,
        corner: Int,
    ): Boolean

    /** Renders every stop on the calling thread; returns once the job is rendered, cancelled or failed. */
    external fun loopJobRun(job: Long)

    /** From any thread. */
    external fun loopJobCancel(job: Long)

    /** 0..1 across the whole reel; from any thread. */
    external fun loopJobProgress(job: Long): Float

    /** One of the `LOOP_JOB_*` states; from any thread. */
    external fun loopJobState(job: Long): Int

    /** Why a failed job failed, one of `LOOP_FAILURE_*`; [loopJobDetail] is the text it is shown with. */
    external fun loopJobFailure(job: Long): Int

    external fun loopJobDetail(job: Long): String?

    /** The frame rate the encoder took. */
    external fun loopJobFps(job: Long): Int

    /** The crossfade the stops kept. */
    external fun loopJobCrossfadeMs(job: Long): Long

    /** One file per stop, in order, once the job is rendered. */
    external fun loopJobFileCount(job: Long): Int

    external fun loopJobFile(
        job: Long,
        index: Int,
    ): String?

    const val LOOP_JOB_RENDERED = 2
    const val LOOP_JOB_CANCELLED = 3
    const val LOOP_FAILURE_ENCODER_REJECTED = 1
    const val LOOP_FAILURE_SHADER_COMPILE = 2
    const val LOOP_FAILURE_LOW_GPU_MEMORY = 3
    const val LOOP_FAILURE_NO_OUTPUT = 4
    const val LOOP_FAILURE_STALLED = 5
    const val LOOP_FAILURE_ENCODER_FAILED = 6
    const val LOOP_FAILURE_CACHE_WRITE = 7

    /** Fields of the long[7] [loopSpecOf] and [loopSpecDerive] fill. */
    const val LOOP_SPEC_LOOP_MS = 0
    const val LOOP_SPEC_CROSSFADE_MS = 1
    const val LOOP_SPEC_FPS = 2
    const val LOOP_SPEC_LOOP_FRAMES = 3
    const val LOOP_SPEC_CROSSFADE_FRAMES = 4
    const val LOOP_SPEC_SOURCE_FRAMES = 5
    const val LOOP_SPEC_LOOP_DURATION_US = 6
    const val LOOP_SPEC_LONGS = 7

    /** Fields of the long[6] [loopLimits] returns. */
    const val LOOP_LIMIT_MIN_LOOP_MS = 0
    const val LOOP_LIMIT_MAX_LOOP_MS = 1
    const val LOOP_LIMIT_MIN_CROSSFADE_MS = 2
    const val LOOP_LIMIT_MAX_CROSSFADE_MS = 3
    const val LOOP_LIMIT_DEFAULT_CROSSFADE_MS = 4
    const val LOOP_LIMIT_MAX_DRIFT_STOPS = 5

    /** The ranges a loop render takes (`core/output/LoopPlan`), in LOOP_LIMIT_* order. */
    external fun loopLimits(): LongArray

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

    /** What [renderEtaSample] answers while it has no estimate yet. */
    const val RENDER_ETA_UNKNOWN = -1L

    /** A render's estimate of the time left, for one run after another. Freed with [renderEtaDestroy]. */
    external fun renderEtaCreate(): Long

    external fun renderEtaDestroy(handle: Long)

    /** Feeds one progress sample; the seconds left, or [RENDER_ETA_UNKNOWN]. [steady] false while warming up. */
    external fun renderEtaSample(
        handle: Long,
        progress: Float,
        atMs: Long,
        steady: Boolean,
    ): Long

    external fun renderEtaReset(handle: Long)

    /** How [renderEtaPhrase] words the seconds left; the last two carry [renderEtaPhraseCount]. */
    const val ETA_ALMOST_DONE = 0
    const val ETA_ABOUT_A_MINUTE = 1
    const val ETA_MINUTES = 2
    const val ETA_HOURS = 3

    external fun renderEtaPhrase(secondsRemaining: Long): Int

    /** The minutes or hours the phrase names; 0 for the other two. */
    external fun renderEtaPhraseCount(secondsRemaining: Long): Int

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

    /** Once [recordSessionState] is `RECORD_SESSION_SAVED` or `_FAILED` and [stageRecordClosed] says the tee is closed. */
    external fun recordSessionDestroy(session: Long)

    /** The encoder's frame into [out]: width, height. */
    external fun recordSessionSize(
        session: Long,
        out: IntArray,
    )

    /** One of `RECORD_SESSION_*`, polled. */
    external fun recordSessionState(session: Long): Int

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

    /**
     * A Studio project's edit (timeline, markers, keyframes; core/studio) read from the JSON project.json keeps
     * under "edit", as canonical JSON to store as it is; null when it is no edit. A null [json] is the empty edit.
     */
    external fun studioEditRead(json: String?): String?

    /** A project's kind read from its "kind" JSON, canonical; null when unknown. A null [json] is the standard kind. */
    external fun studioKindRead(json: String?): String?

    /**
     * A new project's edit: the song [songUri] (null for none) on an audio lane and, with [sceneId], the look over
     * the song or the render ([renderMs]), whichever is longer. Null only when native could not make it.
     */
    external fun studioEditNew(
        songUri: String?,
        songTitle: String,
        songMs: Long,
        sceneId: String?,
        milkPath: String?,
        renderMs: Long,
    ): String?

    /** The shortest clip native makes: a render range shorter than this has no stretch of song to draw over. */
    external fun studioMinClipMs(): Long

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

    /** Fields of each format [captureFormats] returns, CAPTURE_FORMAT_INTS ints a format. */
    const val CAPTURE_FORMAT_RATE = 0
    const val CAPTURE_FORMAT_CHANNELS = 1
    const val CAPTURE_FORMAT_FLOAT = 2
    const val CAPTURE_FORMAT_INTS = 3

    /**
     * The formats a capture tries, first to try first (`core/audio/ingest/CapturePlan`): [deviceRateHz]
     * (0 = unknown) then 48, 44.1 and 22.05 kHz; stereo before mono when [stereo]; float before 16-bit.
     */
    external fun captureFormats(
        deviceRateHz: Int,
        stereo: Boolean,
    ): IntArray

    /** The AudioRecord buffer for a format whose minimum is [minBufferBytes]; 0 = skip the format. */
    external fun captureBufferBytes(
        minBufferBytes: Int,
        channels: Int,
    ): Int

    /** The frames one capture read takes. */
    external fun captureReadFrames(): Int

    // What the player tells the sound engines that the tap cannot hear (core/sound/PlayerSignals).

    /**
     * A new track (or the same one again) begins a new play with [seed]: every live renderer's sound engine
     * forgets the tempo, key and sections at its next frame and fires a full-strength section change, so a
     * crossfaded or gapless next track starts fresh. Returns the boundary's epoch. Any thread.
     */
    external fun soundTrackBoundary(seed: Long): Long

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

    // Settings › Audio's listening tuning (core/sound/Tuning). The shell keeps only the saved floats, null for
    // the engine's defaults, so a reset follows the engine when they change.

    /** `GEODE_SOUND_TUNING_FLOATS`, then each field's first slot (`core/sound/SoundFrame.h`); a band or drum field takes three. */
    const val SOUND_TUNING_FLOATS = 30
    const val SOUND_TUNING_GAIN = 0
    const val SOUND_TUNING_THRESHOLD = 3
    const val SOUND_TUNING_OFFSET = 6
    const val SOUND_TUNING_ATTACK = 9
    const val SOUND_TUNING_RELEASE = 12
    const val SOUND_TUNING_DRUM = 15
    const val SOUND_TUNING_DRUM_DECAY = 18
    const val SOUND_TUNING_REACTIVITY = 21
    const val SOUND_TUNING_SPEED = 22
    const val SOUND_TUNING_ONSET_THRESHOLD = 23
    const val SOUND_TUNING_HARD_CUT_LEVEL = 24
    const val SOUND_TUNING_HARD_CUT_HALF_LIFE = 25
    const val SOUND_TUNING_PITCH_THRESHOLD = 26
    const val SOUND_TUNING_NOTE_HOLD = 27
    const val SOUND_TUNING_SILENCE_DB = 28
    const val SOUND_TUNING_INPUT_TRIM_DB = 29

    /** Every `GeodeSoundTuning` slot's default, then every minimum, then every maximum; null when out of memory. */
    external fun soundTuningSchema(): FloatArray?

    /** Every slot as the engine hears [saved] (any length, null for none): clamped, or the defaults; null when out of memory. */
    external fun soundTuningResolve(saved: FloatArray?): FloatArray?
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

    /**
     * The wordmark's paint for a frame whose short side is [shortSide], into [out] (5 floats): text size px,
     * alpha (0..255), letter spacing (em), shadow radius px, shadow alpha.
     */
    external fun exportWatermarkPaint(
        shortSide: Int,
        placement: Int,
        out: FloatArray,
    )

    /**
     * The tile for the text measured with that paint ([textWidth], the font's [ascent] and [descent] at [textPx])
     * into [out] (7 floats): width, height, rotation in degrees, its pivot x and y, where the text is drawn x and y.
     */
    external fun exportWatermarkRaster(
        textWidth: Float,
        ascent: Float,
        descent: Float,
        textPx: Float,
        placement: Int,
        out: FloatArray,
    )

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

    /**
     * What Customize's transition picker lists, as UTF-8: the built-in ids it offers, RS (U+001E), then the
     * GL Transitions library's names, each list's entries separated by US (U+001F); `core/viz/TransitionLibrary.hpp`.
     * [assets] is the app's `AssetManager`, typed as Any like [vizCreate]'s.
     */
    external fun transitionPicker(assets: Any): ByteArray

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

    /** The mode these values are, or -1 when none is ("Custom"); the Liquid hot policy is not part of a mode. */
    external fun perfModeOf(
        renderScale: Float,
        fpsCap: Int,
        marchScale: Float,
        postFx: Boolean,
        adaptive: Boolean,
    ): Int

    /** The render and march scales Settings › Performance offers, ascending. */
    external fun perfScaleChoices(): FloatArray

    /** A stored Liquid hot-phone policy as the renderer reads it: 0..2, anything else 0. */
    external fun perfLiquidHot(stored: Int): Int

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

    /** What [aiffScanStep] answers: feed it again, the header is read, or this is not an AIFF that plays. */
    const val AIFF_SCAN_MORE = 0
    const val AIFF_SCAN_DONE = 1
    const val AIFF_SCAN_INVALID = -1

    /** The most [aiffScanRead] asks for at once. */
    const val AIFF_SCAN_MAX_READ = 22

    /** Where [aiffScanHeader] puts each value. */
    const val AIFF_HEADER_CHANNELS = 0
    const val AIFF_HEADER_SAMPLE_RATE = 1
    const val AIFF_HEADER_PCM = 2
    const val AIFF_HEADER_BYTES_PER_FRAME = 3
    const val AIFF_HEADER_FRAMES = 4
    const val AIFF_HEADER_DATA_START = 5
    const val AIFF_HEADER_DATA_BYTES = 6
    const val AIFF_HEADER_COUNT = 7

    /** The header's `AIFF_HEADER_PCM`: how the samples lie, 0 for a layout the player has no encoding for. */
    const val AIFF_PCM_S16_LE = 1
    const val AIFF_PCM_S16_BE = 2
    const val AIFF_PCM_S24_BE = 3
    const val AIFF_PCM_S32_BE = 4

    /**
     * The AIFF header walk for a caller that holds the file itself (the player's extractor): skip
     * [aiffScanSkip] bytes, read [aiffScanRead] bytes, hand them to [aiffScanStep], and repeat while it
     * answers [AIFF_SCAN_MORE]. Freed with [aiffScanDestroy].
     */
    external fun aiffScanCreate(): Long

    external fun aiffScanDestroy(handle: Long)

    external fun aiffScanSkip(handle: Long): Long

    external fun aiffScanRead(handle: Long): Int

    /** The first [n] bytes of [bytes], which must be the count [aiffScanRead] asked for. */
    external fun aiffScanStep(
        handle: Long,
        bytes: ByteArray,
        n: Int,
    ): Int

    /** Fills [out] (`AIFF_HEADER_COUNT` values); false before [AIFF_SCAN_DONE]. */
    external fun aiffScanHeader(
        handle: Long,
        out: LongArray,
    ): Boolean

    /** The start time of the frame [bytes] into the samples lies in. */
    external fun aiffTimeUs(
        bytes: Long,
        bytesPerFrame: Int,
        sampleRate: Int,
    ): Long

    /** The byte offset into the samples of the frame playing at [timeUs], held inside [dataBytes]. */
    external fun aiffSeekOffset(
        timeUs: Long,
        bytesPerFrame: Int,
        sampleRate: Int,
        dataBytes: Long,
    ): Long

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

    /** A stream failed for good at [nowMs] (a monotonic clock); native then closes the output for a minute. */
    external fun audioOutNoteFailed(nowMs: Long)

    /** Whether the output may be tried at [nowMs]: false for a minute after [audioOutNoteFailed]. */
    external fun audioOutMayOpen(nowMs: Long): Boolean

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

    // Show on a computer (core/viz/frame/LinkShare, LinkPair): the link's state and the typed path's code; and its
    // QR code (core/util/QrCode). The server, its routes and its pairing are native ([castServerOpen]).

    /** The code to show now ("ABC-DEF"), "" while closed; a new one once the last has lived its time. */
    external fun linkPairCode(nowMs: Long): String

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

    /** [castServerAdmit]'s wants (GEODE_LINK_WANT_*). */
    const val LINK_WANT_TV = 1
    const val LINK_WANT_LINK = 2

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

    /** Opens the link bound to the server's address: [boundDotted], and [bound] (IPv4, host order) with its [prefix] length. */
    external fun webrtcOpen(
        boundDotted: String,
        bound: Int,
        prefix: Int,
    ): Long

    external fun webrtcClose(handle: Long)

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

    // The cast's HTTP server (core/cast/LiveServer, LiveHttp, LiveSegments): the live stream cut from the muxer's
    // bytes and served on the LAN address under a random token, every request read and answered in native. One
    // handle per cast stream; 0 = none.

    /** [castServerText]'s texts (GEODE_CAST_TEXT_*). */
    const val CAST_TEXT_PLAYLIST_URL = 0
    const val CAST_TEXT_SHARE_URL = 1
    const val CAST_TEXT_LABEL = 2
    const val CAST_TEXT_REFUSALS = 3
    const val CAST_TEXT_PAIR_URL = 4

    /** Size of [castServerState]'s array (GEODE_CAST_STATE) and what each long is. */
    const val CAST_STATE = 5
    const val CAST_STATE_LAST_FETCH_MS = 0
    const val CAST_STATE_SEGMENT_AGE_MS = 1
    const val CAST_STATE_READY = 2
    const val CAST_STATE_AS_MUXED = 3
    const val CAST_STATE_BROKEN = 4

    /**
     * Makes the server for the address [boundDotted] ([bound] IPv4 in host order, [prefix] its length); [webrtc] is the
     * stream's [webrtcOpen] handle, asked for the answer to a browser's offer, and must be closed after this one.
     */
    external fun castServerOpen(
        boundDotted: String,
        bound: Int,
        prefix: Int,
        webrtc: Long,
    ): Long

    /** Binds and serves; the port, or 0 when the address could not be bound. */
    external fun castServerStart(handle: Long): Int

    /** Ends every connection and the server's threads; the texts and state stay readable. */
    external fun castServerStop(handle: Long)

    /** Stops and frees. */
    external fun castServerClose(handle: Long)

    /** Who may connect: the Cast device at [tv] (IPv4, host order, 0 = none named) and [wants] LINK_WANT_* bits. */
    external fun castServerAdmit(
        handle: Long,
        tv: Int,
        wants: Int,
    )

    /** The next [size] bytes of the muxer's output, in whatever pieces they come. */
    external fun castServerWrite(
        handle: Long,
        data: ByteArray,
        size: Int,
    )

    /** One of the CAST_TEXT_* texts; "" when there is none. */
    external fun castServerText(
        handle: Long,
        which: Int,
    ): String?

    // A render's plan (core/api/geode_render_plan_api.h).

    /**
     * The part between slider fractions [start] and [end] of [trackMs] into [out]: start ms, duration ms, and end ms
     * when it holds three. False: the whole track.
     */
    external fun exportRangeOf(
        start: Float,
        end: Float,
        trackMs: Long,
        out: LongArray,
    ): Boolean

    /** A range ([durationMs] 0: the whole track) under the free tier's [capMs] (<= 0: none) into [out]; out[1] 0 is still the whole track. */
    external fun exportRangeCapped(
        startMs: Long,
        durationMs: Long,
        capMs: Long,
        out: LongArray,
    )

    /** The shortest part a render is offered. */
    external fun exportRangeMinMs(): Long

    /** The indices of [uris] a batch renders, in order, each URI once. */
    external fun renderBatchOrder(uris: Array<String>): IntArray

    /** A batch's whole progress, 0..1: [done] of [total] tracks plus the share [within] of the one rendering. */
    external fun renderBatchProgress(
        done: Int,
        total: Int,
        within: Float,
    ): Float

    // An export's platform audio work (core/api/geode_export_audio_api.h): blocking, on the caller's thread.

    /**
     * Decodes the finished render at [fd] (detached: native closes it; [offset] and [length] its span, length
     * < 0: the whole descriptor) and reads its loudness for target [target] (0 Leave as-is, 1 YouTube, 2 Shorts).
     * Returns the sentence to word it with (-1: nothing measured) and fills [out] (9 doubles): LUFS, true peak,
     * gain, the gain the level alone needed, resulting LUFS and true peak, the target's ceiling, LU under
     * YouTube worth saying (else 0), peak over YouTube's ceiling (1/0).
     */
    external fun exportLoudnessMeasure(
        fd: Int,
        offset: Long,
        length: Long,
        target: Int,
        out: DoubleArray,
    ): Int

    /**
     * An .lrc or plain lyrics (UTF-8 [text]) as core/library/Lyrics' wire form: "S" or "P" and a newline, then one
     * "<timeMs>\t<text>\n" per line; empty when there are no lyrics.
     */
    external fun lyricsParse(text: ByteArray): ByteArray

    /** The line sung at [positionMs] among ascending [timesMs]; -1 before the first, with none, or when plain. */
    external fun lyricsIndexAt(
        timesMs: LongArray,
        positionMs: Long,
    ): Int

    /** Fills [out] ([CAST_STATE] longs, CAST_STATE_*). */
    external fun castServerState(
        handle: Long,
        out: LongArray,
    )

    /** The stack as Customize › Look draws it: its layers by name and the library's palettes (`geode_palette_stack_editor`). Pure. */
    external fun paletteStackEditor(stack: String): String

    /** [stack] as native keeps it; unreadable text is the unset stack (`geode_palette_stack_normalize`). Pure. */
    external fun paletteStackNormalize(stack: String): String

    /** [stack] with library palette [index] added last (`geode_palette_stack_add_library`). Pure. */
    external fun paletteStackAddLibrary(
        stack: String,
        index: Int,
    ): String

    /** [stack] with the user's own colours [rgb] added last as one palette (`geode_palette_stack_add_colours`). Pure. */
    external fun paletteStackAddColours(
        stack: String,
        name: String,
        rgb: IntArray,
    ): String

    /** [stack] without [layer] (`geode_palette_stack_remove`). Pure. */
    external fun paletteStackRemove(
        stack: String,
        layer: Int,
    ): String

    /** [stack] with [layer] moved [delta] places (`geode_palette_stack_move`). Pure. */
    external fun paletteStackMove(
        stack: String,
        layer: Int,
        delta: Int,
    ): String

    /** [stack] sharing by [blend], SHARE or MIX (`geode_palette_stack_set_blend`). Pure. */
    external fun paletteStackSetBlend(
        stack: String,
        blend: String,
    ): String

    /** [samples] colours of [layer]'s own run (`geode_palette_stack_layer_swatch`). Pure. */
    external fun paletteStackLayerSwatch(
        stack: String,
        layer: Int,
        samples: Int,
    ): IntArray

    /** [samples] colours of library palette [index] (`geode_palette_library_swatch`). Pure. */
    external fun paletteLibrarySwatch(
        index: Int,
        samples: Int,
    ): IntArray

    /** The stack every renderer of the process draws; the unset stack draws the look's own colours (`geode_palette_stack_set`). */
    external fun paletteStackSet(stack: String)

    // A Customize slider's maths (core/viz/SliderMap, `geode_slider_*`): pure, cheap, called per drag event.

    /** True when Reset would move [value] to [default] (NaN for a control with none): more than 1e-4 off it. */
    external fun sliderOffDefault(
        value: Float,
        default: Float,
    ): Boolean

    /** The ticks between [first] and [last] of a whole-number slider. */
    external fun sliderIntSteps(
        first: Int,
        last: Int,
    ): Int

    /** The whole number a stepped slider's [position] means: rounded, clamped into [first]..[last]. */
    external fun sliderIntValue(
        position: Float,
        first: Int,
        last: Int,
    ): Int

    /** [value]'s 0..1 place on a log track over [lo]..[hi]. */
    external fun sliderLogPosition(
        value: Float,
        lo: Float,
        hi: Float,
    ): Float

    /** The value at 0..1 [position] on a log track over [lo]..[hi]: lo * (hi / lo)^position. */
    external fun sliderLogValue(
        position: Float,
        lo: Float,
        hi: Float,
    ): Float

    /**
     * The hand mask for the saved routes [reactions] (null: none saved): 0 with none, [HAND_ALL] when no mask was saved
     * beside them ([hasMask] false), else [savedMask]; -1 when the text is no route list any more (geode_mod_hand_load).
     */
    external fun modHandLoad(
        reactions: String?,
        hasMask: Boolean,
        savedMask: Int,
    ): Int

    /**
     * The range Customize's slider offers for the scene param [name] (core/viz/ControlRange): min and max into
     * [out] (two floats). False, [out] untouched, for a name the schema lacks.
     */
    external fun paramsControlRange(
        name: String,
        out: FloatArray,
    ): Boolean

    /** What to do with a track's bookmark at [positionMs] of [durationMs]: [BOOKMARK_IGNORE], [BOOKMARK_CLEAR] or [BOOKMARK_SAVE]. */
    external fun bookmarkVerdict(
        positionMs: Long,
        durationMs: Long,
    ): Int

    /** Whether a track that just started at [currentPositionMs] may still jump to its bookmark. */
    external fun bookmarkResumesFrom(currentPositionMs: Long): Boolean

    // The native stage (core/stage, core/api/geode_stage_api.h): one surface's render thread, EGL context, vsync
    // and renderer. A handle is an id that is never reused: every call on one that is gone does nothing and
    // answers 0, from any thread. Native never calls up; the shell polls [stageTakeEvents].

    /** [assets] is the AssetManager (Any, as for [vizCreate]); [kind] a STAGE_KIND_*; [capFps] the surface's cap (<= 0 none). 0 when it failed. */
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

    /** The rate the stage draws at now; 0 every vsync. */
    external fun stagePacedFps(stage: Long): Float

    /** The `FRAME_*` events of the frames since the last call, OR-ed. */
    external fun stageTakeEvents(stage: Long): Int

    /** The ring frames hear (a `SampleRing` handle), or 0. Returns once no frame reads the ring it replaced. */
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
        adaptive: Boolean,
        liquidHotPolicy: Int,
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
