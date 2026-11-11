package dev.geode.data

import android.content.SharedPreferences
import dev.geode.data.prefs.PrefType
import dev.geode.data.prefs.PrefsCodec
import dev.geode.data.prefs.prefKeys
import kotlin.math.roundToInt

data class PlayerPrefs(
    val shuffle: Boolean = false,
    val repeatMode: Int = 0,
    val speed: Float = 1f,
    val pitchSemitones: Float = 0f,
    val skipSilence: Boolean = false,
    val pauseOnNoisy: Boolean = true,
    val keepScreenOn: Boolean = false,
    val autoResume: Boolean = false,
    val sleepTimerMinutes: Int = 0,
    val sleepFinishTrack: Boolean = false,
    val fadeMs: Int = 0,
    val replayGainMode: Int = 0,
    val replayGainPreampDb: Float = 0f,
    val replayGainClipGuard: Boolean = true,
    val resumeLongTracks: Boolean = true,
    val bitPerfect: Boolean = false,
    /**
     * How long one track crossfades into the next, in ms; 0 is off, the normal gapless join. Off by
     * default: gapless is what an album expects, and a crossfade is a choice for mixes and playlists.
     */
    val crossfadeMs: Int = 0,
    /**
     * Settings › Playback › Output and more: play through the native Oboe output instead of the
     * platform's AudioTrack. Experimental, so off by default; a format or route it cannot play falls
     * back to the AudioTrack by itself (`audio/OboeAudioOutput`).
     */
    val nativeOutput: Boolean = false,
) {
    fun coerced(): PlayerPrefs =
        copy(
            repeatMode = repeatMode.coerceIn(0, MAX_REPEAT_MODE),
            speed = speed.coerceIn(MIN_SPEED, MAX_SPEED),
            pitchSemitones = pitchSemitones.coerceIn(-MAX_PITCH_SEMITONES, MAX_PITCH_SEMITONES),
            sleepTimerMinutes = sleepTimerMinutes.coerceAtLeast(0),
            fadeMs = fadeMs.coerceIn(0, MAX_FADE_MS),
            crossfadeMs = crossfadeMs.coerceIn(0, MAX_CROSSFADE_MS),
            replayGainMode = replayGainMode.coerceIn(0, MAX_REPLAYGAIN_MODE),
            replayGainPreampDb = replayGainPreampDb.coerceIn(-MAX_REPLAYGAIN_PREAMP_DB, MAX_REPLAYGAIN_PREAMP_DB),
        )

    companion object {
        const val MAX_FADE_MS = 6_000
        const val MAX_CROSSFADE_MS = 12_000

        /** Settings › Playback's crossfade moves in whole seconds (#338). */
        const val CROSSFADE_STEP_MS = 1_000

        /**
         * A crossfade slider position, in seconds, as [crossfadeMs]: rounded to whole seconds, Off
         * (0) below half a second, never past [MAX_CROSSFADE_MS] nor below 0.
         */
        fun crossfadeMsFromSeconds(seconds: Float): Int =
            if (seconds.isNaN()) {
                0
            } else {
                // Clamped in seconds first, so a huge value cannot overflow the multiply.
                seconds.coerceIn(0f, (MAX_CROSSFADE_MS / CROSSFADE_STEP_MS).toFloat()).roundToInt() * CROSSFADE_STEP_MS
            }

        const val MAX_REPEAT_MODE = 2
        const val MIN_SPEED = 0.5f
        const val MAX_SPEED = 2f
        const val MAX_PITCH_SEMITONES = 6f
        const val MAX_REPLAYGAIN_MODE = 2
        const val MAX_REPLAYGAIN_PREAMP_DB = 15f
    }
}

/**
 * The player prefs' synchronous reader and writer, for the one caller that cannot suspend:
 * `PlaybackSession` applies the saved options while it is built (see `GeodeContainer.warmPlaybackPrefs`).
 * Everything else goes through [PlayerPrefsRepository].
 */
class PlayerPrefsStore(
    private val prefs: SharedPreferences,
) {
    fun load(): PlayerPrefs = PlayerPrefsCodec.read(prefs)

    fun save(p: PlayerPrefs) = prefs.edit().also { PlayerPrefsCodec.write(it, p) }.apply()

    internal companion object {
        val PREF_KEYS get() = PlayerPrefsCodec.keys
    }
}

/** How [PlayerPrefs] sit in `geode-player`. */
object PlayerPrefsCodec : PrefsCodec<PlayerPrefs> {
    private const val KEY_SLEEP_FINISH = "sleep_finish_track"
    private const val KEY_FADE_MS = "fade_ms"
    private const val KEY_SHUFFLE = "shuffle"
    private const val KEY_REPEAT = "repeat_mode"
    private const val KEY_SPEED = "speed"
    private const val KEY_PITCH = "pitch_semitones"
    private const val KEY_SKIP_SILENCE = "skip_silence"
    private const val KEY_NOISY = "pause_on_noisy"
    private const val KEY_SCREEN_ON = "keep_screen_on"
    private const val KEY_AUTO_RESUME = "auto_resume"
    private const val KEY_SLEEP_MIN = "sleep_timer_minutes"
    private const val KEY_REPLAYGAIN_MODE = "replaygain_mode"
    private const val KEY_REPLAYGAIN_PREAMP = "replaygain_preamp_db"
    private const val KEY_REPLAYGAIN_CLIP_GUARD = "replaygain_clip_guard"
    private const val KEY_RESUME_LONG = "resume_long_tracks"
    private const val KEY_BIT_PERFECT = "bit_perfect"

    /** Not the native engine's old `crossfade_ms`, removed on write: an install that kept one must not get it back. */
    private const val KEY_CROSSFADE_MS = "playback_crossfade_ms"

    /** Not the removed native player's `native_engine`: this only swaps the output under ExoPlayer. */
    private const val KEY_NATIVE_OUTPUT = "oboe_output"

    override val defaults = PlayerPrefs()

    override val keys =
        prefKeys(GeodePrefsFiles.PLAYER) {
            portable(PrefType.BOOLEAN, KEY_SHUFFLE, KEY_SKIP_SILENCE, KEY_NOISY, KEY_SCREEN_ON, KEY_AUTO_RESUME, KEY_SLEEP_FINISH)
            portable(PrefType.BOOLEAN, KEY_REPLAYGAIN_CLIP_GUARD, KEY_RESUME_LONG, KEY_BIT_PERFECT, KEY_NATIVE_OUTPUT)
            portable(PrefType.INT, KEY_REPEAT, KEY_SLEEP_MIN, KEY_FADE_MS, KEY_REPLAYGAIN_MODE, KEY_CROSSFADE_MS)
            portable(PrefType.FLOAT, KEY_SPEED, KEY_PITCH, KEY_REPLAYGAIN_PREAMP)
            retired(null, "native_engine", "crossfade_ms", "crossfade_curve", "gapless")
        }

    override fun normalize(value: PlayerPrefs): PlayerPrefs = value.coerced()

    override fun read(prefs: SharedPreferences): PlayerPrefs =
        PlayerPrefs(
            shuffle = prefs.getBoolean(KEY_SHUFFLE, false),
            repeatMode = prefs.getInt(KEY_REPEAT, 0),
            speed = prefs.getFloat(KEY_SPEED, 1f),
            pitchSemitones = prefs.getFloat(KEY_PITCH, 0f),
            skipSilence = prefs.getBoolean(KEY_SKIP_SILENCE, false),
            pauseOnNoisy = prefs.getBoolean(KEY_NOISY, true),
            keepScreenOn = prefs.getBoolean(KEY_SCREEN_ON, false),
            autoResume = prefs.getBoolean(KEY_AUTO_RESUME, false),
            sleepTimerMinutes = prefs.getInt(KEY_SLEEP_MIN, 0),
            sleepFinishTrack = prefs.getBoolean(KEY_SLEEP_FINISH, false),
            fadeMs = prefs.getInt(KEY_FADE_MS, 0),
            replayGainMode = prefs.getInt(KEY_REPLAYGAIN_MODE, 0),
            replayGainPreampDb = prefs.getFloat(KEY_REPLAYGAIN_PREAMP, 0f),
            replayGainClipGuard = prefs.getBoolean(KEY_REPLAYGAIN_CLIP_GUARD, true),
            resumeLongTracks = prefs.getBoolean(KEY_RESUME_LONG, true),
            bitPerfect = prefs.getBoolean(KEY_BIT_PERFECT, false),
            crossfadeMs = prefs.getInt(KEY_CROSSFADE_MS, 0),
            nativeOutput = prefs.getBoolean(KEY_NATIVE_OUTPUT, false),
        ).coerced()

    override fun write(
        editor: SharedPreferences.Editor,
        value: PlayerPrefs,
    ) {
        editor
            .putBoolean(KEY_SHUFFLE, value.shuffle)
            .putInt(KEY_REPEAT, value.repeatMode)
            .putFloat(KEY_SPEED, value.speed)
            .putFloat(KEY_PITCH, value.pitchSemitones)
            .putBoolean(KEY_SKIP_SILENCE, value.skipSilence)
            .putBoolean(KEY_NOISY, value.pauseOnNoisy)
            .putBoolean(KEY_SCREEN_ON, value.keepScreenOn)
            .putBoolean(KEY_AUTO_RESUME, value.autoResume)
            .putInt(KEY_SLEEP_MIN, value.sleepTimerMinutes)
            .putBoolean(KEY_SLEEP_FINISH, value.sleepFinishTrack)
            .putInt(KEY_FADE_MS, value.fadeMs)
            .putInt(KEY_REPLAYGAIN_MODE, value.replayGainMode)
            .putFloat(KEY_REPLAYGAIN_PREAMP, value.replayGainPreampDb)
            .putBoolean(KEY_REPLAYGAIN_CLIP_GUARD, value.replayGainClipGuard)
            .putBoolean(KEY_RESUME_LONG, value.resumeLongTracks)
            .putBoolean(KEY_BIT_PERFECT, value.bitPerfect)
            .putInt(KEY_CROSSFADE_MS, value.crossfadeMs)
            .putBoolean(KEY_NATIVE_OUTPUT, value.nativeOutput)
            // The native engine's options, gone with it (ExoPlayer joins tracks gaplessly by itself).
            .remove("native_engine")
            .remove("crossfade_ms")
            .remove("crossfade_curve")
            .remove("gapless")
    }
}

/**
 * Where a long track was left, per track, in the same prefs file as the player options. Only
 * tracks over [LONG_TRACK_MS] get one, and a position within [EDGE_MS] of either end clears it.
 */
class BookmarkStore(
    private val prefs: SharedPreferences,
) {
    fun positionFor(uri: String): Long? = prefs.getLong(keyOf(uri), -1L).takeIf { it > 0L }

    fun note(
        uri: String,
        positionMs: Long,
        durationMs: Long,
    ) {
        if (durationMs < LONG_TRACK_MS) return
        if (positionMs < EDGE_MS || positionMs > durationMs - EDGE_MS) clear(uri) else prefs.edit().putLong(keyOf(uri), positionMs).apply()
    }

    fun clear(uri: String) {
        if (prefs.contains(keyOf(uri))) prefs.edit().remove(keyOf(uri)).apply()
    }

    private fun keyOf(uri: String): String = PREFIX + uri

    companion object {
        const val LONG_TRACK_MS: Long = 20L * 60_000L
        const val EDGE_MS: Long = 10_000L
        private const val PREFIX = "bookmark:"

        internal val PREF_KEYS =
            prefKeys(GeodePrefsFiles.PLAYER) {
                portablePrefix(PrefType.LONG, PREFIX)
            }
    }
}
