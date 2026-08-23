package dev.geode.data

import android.content.SharedPreferences

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
) {
    fun coerced(): PlayerPrefs =
        copy(
            repeatMode = repeatMode.coerceIn(0, MAX_REPEAT_MODE),
            speed = speed.coerceIn(MIN_SPEED, MAX_SPEED),
            pitchSemitones = pitchSemitones.coerceIn(-MAX_PITCH_SEMITONES, MAX_PITCH_SEMITONES),
            sleepTimerMinutes = sleepTimerMinutes.coerceAtLeast(0),
            fadeMs = fadeMs.coerceIn(0, MAX_FADE_MS),
        )

    companion object {
        const val MAX_FADE_MS = 6_000
        const val MAX_REPEAT_MODE = 2
        const val MIN_SPEED = 0.5f
        const val MAX_SPEED = 2f
        const val MAX_PITCH_SEMITONES = 6f
    }
}

class PlayerPrefsStore(
    private val prefs: SharedPreferences,
) {
    fun load(): PlayerPrefs =
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
        ).coerced()

    fun save(p: PlayerPrefs) {
        prefs
            .edit()
            .putBoolean(KEY_SHUFFLE, p.shuffle)
            .putInt(KEY_REPEAT, p.repeatMode)
            .putFloat(KEY_SPEED, p.speed)
            .putFloat(KEY_PITCH, p.pitchSemitones)
            .putBoolean(KEY_SKIP_SILENCE, p.skipSilence)
            .putBoolean(KEY_NOISY, p.pauseOnNoisy)
            .putBoolean(KEY_SCREEN_ON, p.keepScreenOn)
            .putBoolean(KEY_AUTO_RESUME, p.autoResume)
            .putInt(KEY_SLEEP_MIN, p.sleepTimerMinutes)
            .putBoolean(KEY_SLEEP_FINISH, p.sleepFinishTrack)
            .putInt(KEY_FADE_MS, p.fadeMs)
            .apply()
    }

    private companion object {
        const val KEY_SLEEP_FINISH = "sleep_finish_track"
        const val KEY_FADE_MS = "fade_ms"
        const val KEY_SHUFFLE = "shuffle"
        const val KEY_REPEAT = "repeat_mode"
        const val KEY_SPEED = "speed"
        const val KEY_PITCH = "pitch_semitones"
        const val KEY_SKIP_SILENCE = "skip_silence"
        const val KEY_NOISY = "pause_on_noisy"
        const val KEY_SCREEN_ON = "keep_screen_on"
        const val KEY_AUTO_RESUME = "auto_resume"
        const val KEY_SLEEP_MIN = "sleep_timer_minutes"
    }
}
