package dev.geode.data

import android.content.Context
import android.content.SharedPreferences
import dev.geode.engine.bridge.GeodeNative
import dev.geode.util.bestEffort
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

/**
 * Note & key reactions' switch (Settings › Behaviour), persisted, and what the engine is told.
 *
 * The work is native (core/viz/NoteResponse): notes, chords, key and mood drive the style knobs, the note
 * colours, a tint toward the notes' colour, and a gentle shape and distortion. This store only says
 * whether it runs. It is free and on by default: it is how every style hears the melody rather than just
 * the drums, so a new install should see it without hunting for a switch. A saved choice wins over the
 * default, so a user who turned it off keeps it off. How strongly each part answers is per style, in
 * Customize › Advanced › Note & key (Pro). The engine seeds each
 * play from the auto-director's seed ([AutoDirectorStore]) mixed with fresh entropy, so this needs none. Like the director the switch is
 * process-wide in the engine, so the screen, exports, the wallpaper and the dream agree; [GeodeApp] creates
 * the store at start so the saved switch reaches a renderer built before the app's UI.
 *
 * Called on the main thread. [push] is the native call, swappable so the rule is testable off-device.
 */
class NoteResponseStore internal constructor(
    private val prefs: SharedPreferences,
    private val push: (Boolean) -> Unit,
) {
    private val _enabled = MutableStateFlow(prefs.getBoolean(KEY_ENABLED, DEFAULT_ENABLED))

    /** The switch as the user set it, which is also what the engine runs. */
    val enabled: StateFlow<Boolean> = _enabled

    private var pushed: Boolean? = null

    init {
        sync()
    }

    fun setEnabled(on: Boolean) {
        _enabled.value = on
        prefs.edit().putBoolean(KEY_ENABLED, on).apply()
        sync()
    }

    /** Pushes only when the switch actually changes, so a repeated tap costs no JNI call. */
    private fun sync() {
        val next = _enabled.value
        if (next == pushed) return
        pushed = next
        push(next)
    }

    companion object {
        private const val TAG = "NoteResponseStore"
        internal const val KEY_ENABLED = "note_response_enabled"

        /** On for a new install; see the class doc. */
        internal const val DEFAULT_ENABLED = true

        @Volatile private var instance: NoteResponseStore? = null

        fun get(context: Context): NoteResponseStore =
            instance ?: synchronized(this) {
                instance ?: NoteResponseStore(GeodePrefsFiles(context.applicationContext).general) { on ->
                    bestEffort(TAG, "push note response") { GeodeNative.noteResponseSet(on) }
                }.also { instance = it }
            }
    }
}
