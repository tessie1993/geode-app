package dev.geode.ui

import android.content.SharedPreferences
import android.os.SystemClock
import dev.geode.data.LfoStore
import dev.geode.render.AdsrConfig
import dev.geode.render.AdsrEngine
import dev.geode.render.LfoConfig
import dev.geode.render.LfoEngine
import dev.geode.render.ParamBlend
import dev.geode.render.ReactionRoute
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

/**
 * One A/B snapshot: everything the Customize tabs edit, not only the params. Holding just the
 * params made Set/Recall look broken on Events, Reactivity's LFOs and envelopes, the macro knobs
 * and the layer, which each keep their own state.
 */
data class AbLook(
    val params: SceneParams,
    val reactions: List<ReactionRoute>?,
    val lfos: List<LfoConfig>,
    val adsrs: List<AdsrConfig>,
    val knobs: List<Float>,
    val layers: LayersUiState,
)

/** The two A/B snapshots, and where the blend between them currently sits. */
data class AbSnapshotState(
    val a: AbLook? = null,
    val b: AbLook? = null,
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

        /** The macro knob positions, for A/B. */
        val knobs: List<Float>

        fun setSceneParams(params: SceneParams)

        fun setKnobs(values: List<Float>)
    }

    private val _lfos = MutableStateFlow(lfoStore.load())
    val lfos: StateFlow<List<LfoConfig>> = _lfos

    private val _adsrs = MutableStateFlow(lfoStore.loadAdsrs())
    val adsrs: StateFlow<List<AdsrConfig>> = _adsrs

    // Null while the style's DNA plays; the first edit copies the DNA into a list of the user's own.
    private val _reactions = MutableStateFlow(lfoStore.loadReactions())
    val reactions: StateFlow<List<ReactionRoute>?> = _reactions

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

    /**
     * Rolls [tab], or everything when null. Events owns no params of its own, so "Randomize"
     * there means a new style DNA. (Macros are routed to [MacroController] by the session.)
     */
    fun randomizeParams(tab: CustomizeTab? = null) {
        when (tab) {
            CustomizeTab.EVENTS -> return rerollReactions()
            else -> Unit
        }
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
    fun resetTab(tab: CustomizeTab) =
        when (tab) {
            CustomizeTab.EVENTS -> setReactions(null)
            else -> commitSceneParams(ParamRandomizer.resetTab(host.params, tab))
        }

    fun resetAll() = commitSceneParams(SceneParams.DEFAULT)

    fun captureA() = _ab.update { it.copy(a = currentLook()) }

    fun captureB() = _ab.update { it.copy(b = currentLook()) }

    fun recallA() {
        _ab.value.a?.let { applyLook(it, commit = true) }
    }

    fun recallB() {
        _ab.value.b?.let { applyLook(it, commit = true) }
    }

    /**
     * Slides the live look between the two snapshots. One gesture, so one undo entry. Numbers
     * (params, knobs, layer mix) glide; lists and switches flip at the midpoint, as
     * [ParamBlend] does for the params that are not numbers.
     */
    fun blendAb(t: Float) {
        val state = _ab.value
        val a = state.a
        val b = state.b
        if (a == null || b == null) return
        val k = t.coerceIn(0f, 1f)
        _ab.value = state.copy(blend = k)
        val near = if (k < HALF) a else b
        val knobs =
            if (a.knobs.size == b.knobs.size) {
                a.knobs.zip(b.knobs) { x, y -> x + (y - x) * k }
            } else {
                near.knobs
            }
        val look =
            near.copy(
                params = ParamBlend.mix(a.params, b.params, k),
                knobs = knobs,
                layers = near.layers.copy(mix = a.layers.mix + (b.layers.mix - a.layers.mix) * k),
            )
        applyLook(look, commit = false)
    }

    private fun currentLook() = AbLook(host.params, _reactions.value, _lfos.value, _adsrs.value, host.knobs, LayersBus.state.value)

    // Only what differs is written, so a blend drag does not rewrite the stores every frame.
    private fun applyLook(
        look: AbLook,
        commit: Boolean,
    ) {
        if (look.reactions != _reactions.value) setReactions(look.reactions)
        if (look.lfos != _lfos.value) {
            _lfos.value = look.lfos
            lfoStore.save(look.lfos)
        }
        if (look.adsrs != _adsrs.value) {
            _adsrs.value = look.adsrs
            lfoStore.saveAdsrs(look.adsrs)
        }
        if (look.layers != LayersBus.state.value) LayersBus.state.value = look.layers
        if (look.knobs != host.knobs) host.setKnobs(look.knobs)
        if (commit) commitSceneParams(look.params) else editSceneParams(look.params)
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

    /** [routes] null hands the matrix back to the style's DNA. */
    fun setReactions(routes: List<ReactionRoute>?) {
        val list = routes?.take(ReactionRoute.MAX_ROUTES)
        _reactions.value = list
        lfoStore.saveReactions(list)
    }

    /** A new personality for this style: the next DNA variant, and back to DNA if it was custom. */
    fun rerollReactions() {
        setReactions(null)
        commitSceneParams(host.params.copy(reactSeed = host.params.reactSeed + 1))
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

        const val HALF = 0.5f
    }
}
