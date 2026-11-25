package dev.geode.engine.bridge

// The JNI declarations of geode_jni.cpp, and the one that loads libgeode (CLAUDE.md). The other native files' declarations
// are the sibling `*Native` objects of this package, one per JNI source file (geode_viz_jni.cpp's are split by topic).
object GeodeNative {
    init {
        System.loadLibrary("geode")
    }

    /** Touching this object runs its `init`, which loads libgeode; every other `*Native` object calls it from its own `init`. */
    fun ensureLoaded() = Unit

    /** One playback DSP for a player's lifetime; its chains follow the formats [dspConfigure] stages. */
    external fun dspCreate(): Long

    /**
     * Stages the chain for the format queued after the next [dspFlush]; false when it cannot run it
     * ([dspRuns] says no, or no handle), and that format then passes through.
     */
    external fun dspConfigure(
        handle: Long,
        encoding: Int,
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

    /** At most this many tracks keep a bookmark (GEODE_BOOKMARK_MAX, `core/audio/dsp/TrackBookmark` kMaxBookmarks). */
    const val BOOKMARK_MAX: Int = 200

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

    /** Band [band]'s centre as the settings show it ("125 Hz", "1 kHz"). */
    external fun dspBandLabel(band: Int): String

    /** A level in millibels as dB to a tenth ("+3.5 dB", "-12 dB", "0 dB"). */
    external fun dspDbLabel(millibels: Int): String

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

    /** The UI fade (pause, resume, skip) over [ms]: to full level when [audible], else to silence. */
    external fun dspRampAudible(
        handle: Long,
        audible: Boolean,
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

    /** The queue as it stands: [repeatMode] is `Player.REPEAT_MODE_*`; native decides whether something follows on. */
    external fun dspSetUpNext(
        handle: Long,
        hasNext: Boolean,
        repeatMode: Int,
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

    /** The A-B loop after a press at [atMs] (audio/dsp/PlaybackControls), as `[start, end]`; -1 is a point not set. */
    external fun abLoopCycle(
        startMs: Long,
        endMs: Long,
        atMs: Long,
    ): LongArray

    /** Where to seek at [positionMs]: the loop's start once its end is reached, else -1. */
    external fun abLoopSeek(
        startMs: Long,
        endMs: Long,
        positionMs: Long,
    ): Long

    /**
     * Whether the platform takes the next AudioTrack bit-perfect (#353); from the next [dspFlush] the chain
     * passes every block through untouched until a flush with it off.
     */
    external fun dspSetBitPerfect(
        handle: Long,
        on: Boolean,
    )

    /**
     * Copies [bytes] of [input] from [position] into the direct [output] (from 0) and processes the whole frames
     * there, as [encoding] (`C.ENCODING_PCM_*`) at [channels], the format they were queued in; never past its
     * capacity. Returns the bytes to pass on: those whole frames, a trailing part frame dropped.
     */
    external fun dspProcessInto(
        handle: Long,
        input: java.nio.ByteBuffer,
        position: Int,
        bytes: Int,
        output: java.nio.ByteBuffer,
        encoding: Int,
        channels: Int,
    ): Int

    /** Mirrors GEODE_HAND_ALL: every moment and every unclaimed source, the whole list the user's. */
    const val HAND_ALL: Int = 0x400003FF

    /** What to do with a track's bookmark at [positionMs] of [durationMs]: [BOOKMARK_IGNORE], [BOOKMARK_CLEAR] or [BOOKMARK_SAVE]. */
    external fun bookmarkVerdict(
        positionMs: Long,
        durationMs: Long,
    ): Int

    /** Whether a track that just started at [currentPositionMs] may still jump to its bookmark. */
    external fun bookmarkResumesFrom(currentPositionMs: Long): Boolean
}
