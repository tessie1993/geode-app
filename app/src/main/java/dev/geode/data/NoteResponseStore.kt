package dev.geode.data

import android.content.SharedPreferences
import dev.geode.data.prefs.PrefType
import dev.geode.data.prefs.PrefsCodec
import dev.geode.data.prefs.SettingsRepository
import dev.geode.data.prefs.prefKeys
import dev.geode.engine.bridge.GeodeNative
import dev.geode.util.bestEffort
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch

/**
 * Note & key reactions' switch (Settings › Sound reaction), persisted, and what the engine is told.
 *
 * The work is native (core/viz/NoteResponse): notes, chords, key and mood drive the six style knobs each
 * style spends its own way; it writes no scene param and no colour. This store only says
 * whether it runs. It is free and on by default: it is how every style hears the melody rather than just
 * the drums, so a new install should see it without hunting for a switch. A saved choice wins over the
 * default, so a user who turned it off keeps it off. How strongly each part answers is per style, in
 * Customize › Advanced › Note & key (Pro). The engine seeds each
 * play from the auto-director's seed ([AutoDirectorStore]) mixed with fresh entropy, so this needs none. Like the director the switch is
 * process-wide in the engine, so the screen, exports, the wallpaper and the dream agree.
 *
 * One instance per process, built by `GeodeContainer`; the switch is kept by [NoteResponseCodec] in a
 * [SettingsRepository] (#1009, PR 6), read on IO, and pushed once that read is back, then on each change.
 * [push] is the native call, swappable so the rule is testable off-device.
 */
class NoteResponseStore internal constructor(
    private val repository: SettingsRepository<Boolean>,
    private val scope: CoroutineScope,
    private val push: (Boolean) -> Unit = { on -> bestEffort(TAG, "push note response") { GeodeNative.noteResponseSet(on) } },
) {
    /** The switch as the user set it, which is also what the engine runs. */
    val enabled: StateFlow<Boolean> = repository.value

    private var pushed: Boolean? = null

    init {
        scope.launch {
            repository.loaded()
            repository.value.collect { sync(it) }
        }
    }

    fun setEnabled(on: Boolean) {
        scope.launch { repository.update { on } }
    }

    /** Pushes only when the switch actually changes, so a repeated tap costs no JNI call. */
    private fun sync(next: Boolean) {
        if (next == pushed) return
        pushed = next
        push(next)
    }

    private companion object {
        const val TAG = "NoteResponseStore"
    }
}

/** The switch in `geode-prefs`; on for a new install (see [NoteResponseStore]). */
object NoteResponseCodec : PrefsCodec<Boolean> {
    internal const val KEY_ENABLED = "note_response_enabled"

    override val defaults = true

    override val keys = prefKeys(GeodePrefsFiles.GENERAL) { portable(PrefType.BOOLEAN, KEY_ENABLED) }

    override fun read(prefs: SharedPreferences) = prefs.getBoolean(KEY_ENABLED, defaults)

    override fun write(
        editor: SharedPreferences.Editor,
        value: Boolean,
    ) {
        editor.putBoolean(KEY_ENABLED, value)
    }
}
