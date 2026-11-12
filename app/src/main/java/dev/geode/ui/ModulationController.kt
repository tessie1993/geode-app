package dev.geode.ui

import android.content.SharedPreferences
import android.os.SystemClock
import dev.geode.data.GeodePrefsFiles
import dev.geode.data.LfoStore
import dev.geode.data.prefs.PrefType
import dev.geode.data.prefs.prefKeys
import dev.geode.engine.bridge.GeodeNative
import dev.geode.render.AdsrConfig
import dev.geode.render.AdsrEngine
import dev.geode.render.HandMask
import dev.geode.render.HandReactions
import dev.geode.render.LfoConfig
import dev.geode.render.LfoEngine
import dev.geode.render.LfoTarget
import dev.geode.render.LfoWave
import dev.geode.render.ModCurve
import dev.geode.render.ModPolarity
import dev.geode.render.ModSource
import dev.geode.render.ReactionRoute
import dev.geode.render.bridge.ModConfigCodec
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

    private val _lfos = MutableStateFlow(lfoStore.load())
    val lfos: StateFlow<List<LfoConfig>> = _lfos

    private val _adsrs = MutableStateFlow(lfoStore.loadAdsrs())
    val adsrs: StateFlow<List<AdsrConfig>> = _adsrs

    // Null while the style's DNA plays; the first edit copies the DNA into a list of the user's own.
    private val _reactions = MutableStateFlow(lfoStore.loadReactions())
    val reactions: StateFlow<List<ReactionRoute>?> = _reactions

    // Which moments that list owns (Customize › Music: the hand wins per moment); saved beside it and
    // pushed process-wide, where every renderer merges with the live DNA and every director keeps off.
    private val _handMask = MutableStateFlow(lfoStore.loadHandMask().also { HandMask.set(it, _reactions.value != null) })
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
     * steady waves already on and holds them when Music is kept. This only carries the slots there and back.
     */
    fun randomizeWaves(all: Boolean) {
        val floats = ModConfigCodec.packLfo(LfoConfig()).size
        val input = FloatArray(LfoEngine.SLOTS * floats)
        _lfos.value.take(LfoEngine.SLOTS).forEachIndexed { i, c -> ModConfigCodec.packLfo(c).copyInto(input, i * floats) }
        val out = FloatArray(input.size)
        val locked = _lockedParams.value.joinToString("\n")
        val params = SceneParamsCodec.pack(host.params)
        if (!GeodeNative.wavesRandomize(host.sceneId, locked, Random.nextLong(), !all, params, input, out)) return
        val rolled =
            List(LfoEngine.SLOTS) { i ->
                val o = i * floats
                LfoConfig(
                    enabled = out[o] > 0.5f,
                    source = ModSource.entries[out[o + 1].toInt()],
                    target = LfoTarget.entries[out[o + 2].toInt()],
                    wave = LfoWave.entries[out[o + 3].toInt()],
                    rateSeconds = out[o + 4],
                    depth = out[o + 5],
                    polarity = ModPolarity.entries[out[o + 6].toInt()],
                    curve = ModCurve.entries[out[o + 7].toInt()],
                    beats = out[o + 8].toInt(),
                    knob = out[o + 9].toInt(),
                    moment = out[o + 10].toInt(),
                )
            }
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
        if (category == CustomizeCategory.MUSIC) setReactions(null)
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

    /**
     * [routes] null hands the matrix back to the style's DNA. A list from the route editor or a recipe is
     * the whole matrix, so it owns every moment ([GeodeNative.HAND_ALL]).
     */
    fun setReactions(routes: List<ReactionRoute>?) = setHandReactions(HandReactions(routes, GeodeNative.HAND_ALL))

    /** Customize › Music's edit, as native worked it out: the user's routes and the moments they own. */
    fun setHandReactions(edit: HandReactions) {
        val list = edit.routes?.take(ReactionRoute.MAX_ROUTES)
        val mask = if (list == null) 0 else edit.mask
        _reactions.value = list
        _handMask.value = mask
        HandMask.set(mask, list != null)
        lfoStore.saveReactions(list, mask)
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
