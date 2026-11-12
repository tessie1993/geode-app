package dev.geode.data

import android.content.SharedPreferences
import dev.geode.engine.bridge.GeodeNative
import dev.geode.render.TransitionCatalog
import dev.geode.render.VisualizerRenderer
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import java.util.concurrent.atomic.AtomicBoolean

internal class VizStateStore(
    private val prefs: SharedPreferences,
    private val scope: CoroutineScope,
    autoVisualsPrefsStore: AutoVisualsPrefsStore,
) {
    val state = MutableStateFlow(restore(autoVisualsPrefsStore))

    init {
        VisualizerRenderer.seedMilkMemory(prefs)
    }

    // Starts on the preset the last run showed: the renderer restores that one itself (from the
    // same native memory), and the Styles list, templates and takes must agree with the screen.
    private val _activeMilkPath = MutableStateFlow(GeodeNative.milkMemoryLastShown())
    val activeMilkPath: StateFlow<String?> = _activeMilkPath

    /**
     * The style the user last chose, held while the Styles grid previews another one.
     *
     * A preview puts its style in [state] so the stage shows it, but it is not a choice yet: a
     * slider moved meanwhile still persists, and must not save the previewed style with it (a
     * locked Pro style would then come back after a restart, only to be swapped out again).
     */
    @Volatile
    var committedSceneId: String? = null

    @Volatile
    private var dirty = false

    private val scheduled = AtomicBoolean(false)

    private fun restore(store: AutoVisualsPrefsStore): VizUiState {
        var base = store.applyTo(VizUiState(presets = BuiltInPresets.ALL))
        // Restore the transition id first, then re-derive the style from it (falling back to
        // whatever default the style key carries) so the two never disagree after a restore.
        val transitionId = prefs.getString(KEY_TRANSITION_ID, null)
        if (transitionId != null) {
            base =
                base.copy(
                    transitionId = transitionId,
                    transitionStyle = TransitionCatalog.builtIn(transitionId) ?: base.transitionStyle,
                )
        }
        if (prefs.contains(KEY_TRANSITION_DURATION_SEC)) {
            base = base.copy(transitionDurationSec = prefs.getFloat(KEY_TRANSITION_DURATION_SEC, base.transitionDurationSec))
        }
        val json = prefs.getString(KEY_LIVE_STATE, null) ?: return base
        return runCatching {
            val p = PresetStore.fromJson(json)
            base.copy(sceneId = p.sceneId, attack = p.attack, decay = p.decay, params = p.params)
        }.getOrDefault(base)
    }

    fun persist() {
        dirty = true
        if (!scheduled.compareAndSet(false, true)) return
        scope.launch(Dispatchers.IO) {
            delay(PERSIST_WINDOW_MS)
            scheduled.set(false)
            write()
        }
    }

    fun flushIfDirty() {
        if (dirty) write()
    }

    private fun write() {
        dirty = false
        val s = state.value
        val sceneId = committedSceneId ?: s.sceneId
        val json = PresetStore.toJson(Preset("__live__", sceneId, s.attack, s.decay, null, s.params))
        prefs
            .edit()
            .putString(KEY_LIVE_STATE, json)
            .putString(KEY_TRANSITION_ID, s.transitionId)
            .putFloat(KEY_TRANSITION_DURATION_SEC, s.transitionDurationSec)
            .commit()
    }

    /** The renderer reported [path]; it has already saved it. */
    fun noteMilkPreset(path: String) {
        _activeMilkPath.value = path
    }

    fun dropRemovedMilkPaths(paths: List<String>) {
        if (_activeMilkPath.value in paths) _activeMilkPath.value = null
        if (GeodeNative.milkMemoryForget(paths.toTypedArray())) VisualizerRenderer.saveMilkPath(prefs, null)
    }

    private companion object {
        // The keys are LookStore's, which the wallpaper, the dream and Cast read the saved look through.
        const val KEY_LIVE_STATE = LookStore.KEY_LIVE_STATE
        const val KEY_TRANSITION_ID = LookStore.KEY_TRANSITION_ID
        const val KEY_TRANSITION_DURATION_SEC = LookStore.KEY_TRANSITION_DURATION_SEC
        const val PERSIST_WINDOW_MS = 400L
    }
}
