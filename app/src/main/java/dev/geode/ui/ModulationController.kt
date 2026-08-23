package dev.geode.ui

import android.content.SharedPreferences
import android.os.SystemClock
import dev.geode.data.LfoStore
import dev.geode.render.AdsrConfig
import dev.geode.render.AdsrEngine
import dev.geode.render.LfoConfig
import dev.geode.render.LfoEngine
import dev.geode.render.ParamBlend
import dev.geode.render.scene.CustomizeTab
import dev.geode.render.scene.ParamRandomizer
import dev.geode.render.scene.SceneParams
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.update

/** What the panel's undo/redo buttons should look like right now. */
data class ParamHistoryState(
    val canUndo: Boolean = false,
    val canRedo: Boolean = false,
)

/** The two A/B snapshots, and where the blend between them currently sits. */
data class AbSnapshotState(
    val a: SceneParams? = null,
    val b: SceneParams? = null,
    val blend: Float = 0f,
) {
    val canBlend: Boolean get() = a != null && b != null
}

/**
 * Everything the Customize panel does to a parameter set that is not "type a new number".
 *
 * Modulation slots and envelopes, the per-parameter randomize locks, the panel's undo/redo
 * history, and the A/B snapshots. They live together because they all edit the SAME thing — the
 * one live parameter set — and splitting them would mean three places that each have to remember
 * to record history.
 */
internal class ModulationController(
    private val lfoStore: LfoStore,
    private val prefs: SharedPreferences,
    private val host: Host,
) {
    interface Host {
        val params: SceneParams

        val sceneId: String

        fun setSceneParams(params: SceneParams)
    }

    private val _lfos = MutableStateFlow(lfoStore.load())
    val lfos: StateFlow<List<LfoConfig>> = _lfos

    private val _adsrs = MutableStateFlow(lfoStore.loadAdsrs())
    val adsrs: StateFlow<List<AdsrConfig>> = _adsrs

    private val _lockedParams =
        MutableStateFlow<Set<String>>(prefs.getStringSet("locked_params", emptySet()) ?: emptySet())
    val lockedParams: StateFlow<Set<String>> = _lockedParams

    private val _history = MutableStateFlow(ParamHistoryState())
    val history: StateFlow<ParamHistoryState> = _history

    private val _ab = MutableStateFlow(AbSnapshotState())
    val ab: StateFlow<AbSnapshotState> = _ab

    private val undoStack = ArrayDeque<SceneParams>()
    private val redoStack = ArrayDeque<SceneParams>()
    private var lastCheckpointMs = 0L

    fun toggleParamLock(label: String) {
        _lockedParams.update { if (label in it) it - label else it + label }
        prefs.edit().putStringSet("locked_params", _lockedParams.value).apply()
    }

    /**
     * The single write path for a panel edit.
     *
     * Every control goes through here so undo covers the whole panel rather than the handful of
     * places somebody remembered to wire. Consecutive edits inside [CHECKPOINT_GAP_MS] fold into
     * one entry, because a slider drag is one gesture and one undo, not four hundred.
     */
    fun editSceneParams(params: SceneParams) {
        val previous = host.params
        if (params == previous) return
        val now = SystemClock.elapsedRealtime()
        if (now - lastCheckpointMs > CHECKPOINT_GAP_MS) {
            pushUndo(previous)
        }
        lastCheckpointMs = now
        host.setSceneParams(params)
    }

    /** A discrete edit — a preset load, a randomize, a reset — always its own undo entry. */
    fun commitSceneParams(params: SceneParams) {
        val previous = host.params
        if (params == previous) return
        pushUndo(previous)
        lastCheckpointMs = 0L
        host.setSceneParams(params)
    }

    fun undo() {
        val previous = undoStack.removeLastOrNull() ?: return
        redoStack.addLast(host.params)
        trim(redoStack)
        lastCheckpointMs = 0L
        host.setSceneParams(previous)
        publishHistory()
    }

    fun redo() {
        val next = redoStack.removeLastOrNull() ?: return
        undoStack.addLast(host.params)
        trim(undoStack)
        lastCheckpointMs = 0L
        host.setSceneParams(next)
        publishHistory()
    }

    fun randomizeParams(tab: CustomizeTab? = null) {
        commitSceneParams(
            ParamRandomizer.randomize(
                current = host.params,
                locked = _lockedParams.value,
                tab = tab,
                sceneId = host.sceneId,
            ),
        )
    }

    /** Puts one tab's parameters back to their defaults and leaves every other tab alone. */
    fun resetTab(tab: CustomizeTab) = commitSceneParams(ParamRandomizer.resetTab(host.params, tab))

    fun resetAll() = commitSceneParams(SceneParams.DEFAULT)

    fun captureA() = _ab.update { it.copy(a = host.params) }

    fun captureB() = _ab.update { it.copy(b = host.params) }

    fun recallA() {
        _ab.value.a?.let(::commitSceneParams)
    }

    fun recallB() {
        _ab.value.b?.let(::commitSceneParams)
    }

    /** Slides the live look between the two snapshots. One gesture, so one undo entry. */
    fun blendAb(t: Float) {
        val state = _ab.value
        val a = state.a
        val b = state.b
        if (a == null || b == null) return
        _ab.value = state.copy(blend = t.coerceIn(0f, 1f))
        editSceneParams(ParamBlend.mix(a, b, t))
    }

    fun setAdsr(
        index: Int,
        config: AdsrConfig,
    ) {
        val list = _adsrs.value.toMutableList()
        while (list.size < AdsrEngine.COUNT) list.add(AdsrConfig())
        if (index in list.indices) {
            list[index] = config
            _adsrs.value = list
            lfoStore.saveAdsrs(list)
        }
    }

    fun setLfo(
        index: Int,
        config: LfoConfig,
    ) {
        val list = _lfos.value.toMutableList()
        while (list.size < LfoEngine.SLOTS) list.add(LfoConfig())
        if (index in 0 until LfoEngine.SLOTS) {
            list[index] = config
            _lfos.value = list
            lfoStore.save(list)
        }
    }

    private fun pushUndo(params: SceneParams) {
        undoStack.addLast(params)
        trim(undoStack)
        redoStack.clear()
        publishHistory()
    }

    private fun trim(stack: ArrayDeque<SceneParams>) {
        while (stack.size > HISTORY_DEPTH) stack.removeFirst()
    }

    private fun publishHistory() {
        _history.value = ParamHistoryState(canUndo = undoStack.isNotEmpty(), canRedo = redoStack.isNotEmpty())
    }

    private companion object {
        const val HISTORY_DEPTH = 64

        const val CHECKPOINT_GAP_MS = 700L
    }
}
