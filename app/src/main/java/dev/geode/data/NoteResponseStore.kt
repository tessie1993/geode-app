package dev.geode.data

import android.content.Context
import android.content.SharedPreferences
import dev.geode.engine.bridge.GeodeNative
import dev.geode.pro.FeatureGate
import dev.geode.pro.NoteResponsePolicy
import dev.geode.util.bestEffort
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

/**
 * Note & key reactions' switch (Settings › Behaviour), persisted, and what the engine is told.
 *
 * The work is native (core/viz/NoteResponse): notes, chords, key and mood drive the style knobs, the note
 * colours, a bloom per note over every style, and a gentle shape and distortion. This store only says
 * whether it runs: the user's switch, gated by [NoteResponsePolicy] (Pro and the trial). The play's seed is
 * the auto-director's ([AutoDirectorStore]), so this needs none of its own. Like the director the switch
 * is process-wide in the engine, so the screen, exports, the wallpaper and the dream agree; [GeodeApp]
 * creates the store at start so the saved switch reaches a renderer built before the app's UI.
 *
 * The entitlement arrives from outside ([onGate]): [dev.geode.playback.AutoDirectorFeed] passes it with
 * every play and every second of playback, and the settings screen passes the live one, so a purchase or
 * a lapsed trial reaches the engine without a restart. Until then the gate is free, so nothing runs that
 * the user has not been seen to own.
 *
 * Called on the main thread. [push] is the native call, swappable so the rule is testable off-device.
 */
class NoteResponseStore internal constructor(
    private val prefs: SharedPreferences,
    private val push: (Boolean) -> Unit,
) {
    private val _enabled = MutableStateFlow(prefs.getBoolean(KEY_ENABLED, false))

    /** The switch as the user set it, whatever the entitlement. */
    val enabled: StateFlow<Boolean> = _enabled

    private var gate = FeatureGate.FREE
    private var pushed: Boolean? = null

    init {
        sync()
    }

    fun setEnabled(on: Boolean) {
        _enabled.value = on
        prefs.edit().putBoolean(KEY_ENABLED, on).apply()
        sync()
    }

    /** The current entitlement; pushes only when whether the layer runs changes. */
    fun onGate(gate: FeatureGate) {
        this.gate = gate
        sync()
    }

    /** Whether the engine is running the layer now. */
    val active: Boolean get() = NoteResponsePolicy.active(_enabled.value, gate)

    private fun sync() {
        val next = active
        if (next == pushed) return
        pushed = next
        push(next)
    }

    companion object {
        private const val TAG = "NoteResponseStore"
        internal const val KEY_ENABLED = "note_response_enabled"

        @Volatile private var instance: NoteResponseStore? = null

        fun get(context: Context): NoteResponseStore =
            instance ?: synchronized(this) {
                instance ?: NoteResponseStore(GeodePrefsFiles(context.applicationContext).general) { on ->
                    bestEffort(TAG, "push note response") { GeodeNative.noteResponseSet(on) }
                }.also { instance = it }
            }
    }
}
