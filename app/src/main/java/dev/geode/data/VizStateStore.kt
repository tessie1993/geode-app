package dev.geode.data

import android.content.SharedPreferences
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow

internal class VizStateStore(
    private val prefs: SharedPreferences,
    private val scope: CoroutineScope,
    autoVisualsPrefsStore: AutoVisualsPrefsStore,
) {
    val state = MutableStateFlow(restore(autoVisualsPrefsStore))

    /**
     * The style the user last chose, held while the Styles grid previews another one.
     *
     * A preview puts its style in [state] so the stage shows it, but it is not a choice yet: a
     * slider moved meanwhile still persists, and must not save the previewed style with it (a
     * locked Pro style would then come back after a restart, only to be swapped out again).
     */
    @Volatile
    var committedSceneId: String? = null

    // What the last write put on disk: an unchanged look is not written again.
    private var written: List<Any?>? = null

    private val coalescer =
        WriteCoalescer(scope, PERSIST_QUIET_MS, PERSIST_MAX_WAIT_MS, Dispatchers.IO) { write() }

    private fun restore(store: AutoVisualsPrefsStore): VizUiState {
        var base = store.applyTo(VizUiState(presets = BuiltInPresets.ALL))
        val transitionId = prefs.getString(KEY_TRANSITION_ID, null)
        if (transitionId != null) base = base.copy(transitionId = transitionId)
        if (prefs.contains(KEY_TRANSITION_DURATION_SEC)) {
            base = base.copy(transitionDurationSec = prefs.getFloat(KEY_TRANSITION_DURATION_SEC, base.transitionDurationSec))
        }
        val json = prefs.getString(KEY_LIVE_STATE, null) ?: return base
        return runCatching {
            val p = PresetStore.fromJson(json)
            base.copy(sceneId = p.sceneId, attack = p.attack, decay = p.decay, params = p.params)
        }.getOrDefault(base)
    }

    /** A look change: written once the changes pause, and at least every [PERSIST_MAX_WAIT_MS] through a long drag. */
    fun persist() = coalescer.request()

    fun flushIfDirty() = coalescer.flush()

    @Synchronized
    private fun write() {
        val s = state.value
        val sceneId = committedSceneId ?: s.sceneId
        val json = PresetStore.toJson(Preset("__live__", sceneId, s.attack, s.decay, null, s.params))
        val now = listOf(json, s.transitionId, s.transitionDurationSec)
        if (now == written) return
        written = now
        prefs
            .edit()
            .putString(KEY_LIVE_STATE, json)
            .putString(KEY_TRANSITION_ID, s.transitionId)
            .putFloat(KEY_TRANSITION_DURATION_SEC, s.transitionDurationSec)
            .commit()
    }

    private companion object {
        // The keys are LookStore's, which Cast reads the saved look through.
        const val KEY_LIVE_STATE = LookStore.KEY_LIVE_STATE
        const val KEY_TRANSITION_ID = LookStore.KEY_TRANSITION_ID
        const val KEY_TRANSITION_DURATION_SEC = LookStore.KEY_TRANSITION_DURATION_SEC
        const val PERSIST_QUIET_MS = 400L
        const val PERSIST_MAX_WAIT_MS = 2_000L
    }
}
