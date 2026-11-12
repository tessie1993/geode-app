package dev.geode.ui

import android.content.SharedPreferences
import android.os.SystemClock
import dev.geode.data.GeodePrefsFiles
import dev.geode.data.LfoStore
import dev.geode.data.prefs.PrefType
import dev.geode.data.prefs.prefKeys
import dev.geode.engine.bridge.GeodeNative
import dev.geode.render.bridge.SceneParamsCodec
import dev.geode.render.scene.SceneParams
import dev.geode.util.bestEffort
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.update
import kotlin.random.Random

/** What the panel's undo/redo buttons should look like right now. */
data class ParamHistoryState(
    val canUndo: Boolean = false,
    val canRedo: Boolean = false,
)

/**
 * Everything the Customize panel does to a parameter set that is not "type a new number".
 *
 * Modulation slots and envelopes, the per-parameter randomize locks and the panel's undo/redo
 * history. They live together because they all edit the SAME thing — the
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

    // The waves, the envelopes and the routes as saved: native reads and edits them (core/viz/ModDocument).
    private val _lfos = MutableStateFlow(lfoStore.load())
    val lfos: StateFlow<String> = _lfos

    private val _adsrs = MutableStateFlow(lfoStore.loadAdsrs())
    val adsrs: StateFlow<String> = _adsrs

    // Null while the style's DNA plays; the first edit copies the DNA into a list of the user's own.
    private val _reactions = MutableStateFlow(lfoStore.loadReactions())
    val reactions: StateFlow<String?> = _reactions

    // Which moments that list owns (Customize › Music: the hand wins per moment); saved beside it and
    // pushed process-wide, where every renderer merges with the live DNA and every director keeps off.
    private val _handMask = MutableStateFlow(lfoStore.loadHandMask().also { lfoStore.pushHand(it, _reactions.value != null) })
    val handMask: StateFlow<Int> = _handMask

    private val _lockedParams =
        MutableStateFlow<Set<String>>(prefs.getStringSet(KEY_LOCKED_PARAMS, emptySet()) ?: emptySet())
    val lockedParams: StateFlow<Set<String>> = _lockedParams

    private val _history = MutableStateFlow(ParamHistoryState())
    val history: StateFlow<ParamHistoryState> = _history

    private val undoStack = ArrayDeque<SceneParams>()
    private val redoStack = ArrayDeque<SceneParams>()
    private var lastCheckpointMs = 0L

    fun toggleParamLock(label: String) {
        _lockedParams.update { if (label in it) it - label else it + label }
        prefs.edit().putStringSet(KEY_LOCKED_PARAMS, _lockedParams.value).apply()
        // The director's reach leaves a kept control alone too (AutoDirectorStore pushes the saved set at start).
        bestEffort(TAG, "push keep set") { GeodeNative.directorKeepSet(_lockedParams.value.joinToString("\n")) }
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
     * Rolls [category], or everything when null. The roll is native (core/viz/CustomizeMap over
     * core/viz/ParamRandomizer): the category's own controls that reach the style, minus the kept
     * ones; this only carries the frame there and back. The knobs are not params: Quick knobs
     * rolls their turns through MacroStore, and has no roll here. [basicsOnly] is the roll
     * without Pro: native holds everything below a Basics (its `#PRO` lock) and the waves stay.
     * True when the look changed.
     */
    fun randomizeParams(
        category: CustomizeCategory? = null,
        basicsOnly: Boolean = false,
    ): Boolean {
        if (category == CustomizeCategory.QUICK_KNOBS) return false
        val input = SceneParamsCodec.pack(host.params)
        val out = FloatArray(SceneParamsCodec.FIELDS)
        val locked = (if (basicsOnly) _lockedParams.value + PRO_LOCK else _lockedParams.value).joinToString("\n")
        val seed = Random.nextLong()
        val changed =
            if (category == null) {
                GeodeNative.paramsRandomize(host.sceneId, null, locked, seed, input, out)
            } else {
                GeodeNative.customizeRandomize(host.sceneId, category.name, locked, seed, input, out)
            }
        if (changed) commitSceneParams(SceneParamsCodec.unpack(out, host.params))
        if (!basicsOnly && (category == null || category == CustomizeCategory.MUSIC)) randomizeWaves(all = false)
        return changed
    }

    /**
     * Randomize everything's roll (core/viz/RandomizeAll), after its style and preset are on screen:
     * the controls Customize keeps go back to their values in [before], then every category is rolled
     * from [seed] and the steady waves already on with it. One undo entry. [basicsOnly] is the dice's
     * free rule, as in [randomizeParams]: native's `#PRO` lock rolls the free Basics only, and the waves stay.
     */
    fun randomizeAllRoll(
        before: SceneParams,
        seed: Long,
        basicsOnly: Boolean,
    ) {
        val out = FloatArray(SceneParamsCodec.FIELDS)
        val locked = (if (basicsOnly) _lockedParams.value + PRO_LOCK else _lockedParams.value).joinToString("\n")
        val base = SceneParamsCodec.pack(host.params)
        if (GeodeNative.randomizeAllRoll(host.sceneId, locked, seed, true, SceneParamsCodec.pack(before), base, out)) {
            commitSceneParams(SceneParamsCodec.unpack(out, host.params))
        }
        if (!basicsOnly) randomizeWaves(all = false)
    }

    /**
     * Randomize for the waves. The roll is native (core/viz/WaveRandomizer): [all] is the wave editor's
     * button, every wave switched on and rolled; otherwise a category roll, which only re-rolls the
     * steady waves already on and holds them when Music is kept. This only carries the waves there and back.
     */
    fun randomizeWaves(all: Boolean) {
        val locked = _lockedParams.value.joinToString("\n")
        val params = SceneParamsCodec.pack(host.params)
        val rolled = GeodeNative.modWavesRandomize(host.sceneId, locked, Random.nextLong(), !all, params, _lfos.value) ?: return
        _lfos.value = rolled
        lfoStore.save(rolled)
    }

    /**
     * Puts one category's controls back to their defaults (native: every field its controls
     * write) and leaves every other category alone. Music's Reset also drops the user's own
     * reaction routes, which are not params.
     */
    fun resetCategory(category: CustomizeCategory) {
        if (category == CustomizeCategory.QUICK_KNOBS) return
        if (category == CustomizeCategory.MUSIC) editReactions(GeodeNative.MOD_EDIT_CLEAR)
        val out = FloatArray(SceneParamsCodec.FIELDS)
        GeodeNative.customizeReset(category.name, SceneParamsCodec.pack(host.params), out)
        commitSceneParams(SceneParamsCodec.unpack(out, host.params))
    }

    /** Takes one category's controls from another look ([from], a preset's params) and leaves the rest. Native. */
    fun borrowCategory(
        category: CustomizeCategory,
        from: SceneParams,
    ) {
        if (category == CustomizeCategory.QUICK_KNOBS) return
        val out = FloatArray(SceneParamsCodec.FIELDS)
        GeodeNative.customizeBorrow(category.name, SceneParamsCodec.pack(host.params), SceneParamsCodec.pack(from), out)
        commitSceneParams(SceneParamsCodec.unpack(out, host.params))
    }

    fun resetAll() = commitSceneParams(SceneParams.DEFAULT)

    /** One [field] (a `GeodeNative.MOD_ADSR_*`) of the envelope at [index]; native sets it and keeps it in range. */
    fun setAdsr(
        index: Int,
        field: Int,
        value: Float,
    ) {
        val next = GeodeNative.modAdsrEdit(_adsrs.value, index, field, value)
        _adsrs.value = next
        lfoStore.saveAdsrs(next)
    }

    /** One [field] (a `GeodeNative.MOD_LFO_*`) of the wave at [index]; native sets it and keeps it in range. */
    fun setLfo(
        index: Int,
        field: Int,
        value: Float,
    ) {
        val next = GeodeNative.modLfoEdit(_lfos.value, index, field, value)
        _lfos.value = next
        lfoStore.save(next)
    }

    /**
     * One edit of the reactions (a `GeodeNative.MOD_EDIT_*` [op] with its [a], [b] and [value]). Native works
     * out the user's routes and the moments they own from what plays on this style; null hands the matrix
     * back to the style's DNA.
     */
    fun editReactions(
        op: Int,
        a: Int = 0,
        b: Int = 0,
        value: Float = 0f,
    ) {
        val outMask = IntArray(1)
        val seed = host.params.reactSeed
        val routes = GeodeNative.modReactionsEdit(host.sceneId, seed, _reactions.value, _handMask.value, op, a, b, value, outMask)
        val mask = if (routes == null) 0 else outMask[0]
        _reactions.value = routes
        _handMask.value = mask
        lfoStore.pushHand(mask, routes != null)
        lfoStore.saveReactions(routes, mask)
    }

    /** A new personality for this style: the next DNA variant, and back to DNA if it was custom. */
    fun rerollReactions() {
        editReactions(GeodeNative.MOD_EDIT_CLEAR)
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

    companion object {
        private const val TAG = "ModulationController"

        /** Native's lock for every control below a Basics (core/viz/CustomizeMap `kProLock`). */
        const val PRO_LOCK = "#PRO"

        private const val HISTORY_DEPTH = 64

        private const val CHECKPOINT_GAP_MS = 700L

        private const val KEY_LOCKED_PARAMS = "locked_params"

        val PREF_KEYS =
            prefKeys(GeodePrefsFiles.MODULATION) {
                portable(PrefType.STRING_SET, KEY_LOCKED_PARAMS)
            }
    }
}
