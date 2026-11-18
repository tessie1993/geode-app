package dev.geode.ui

import android.content.SharedPreferences
import android.os.SystemClock
import dev.geode.data.GeodePrefsFiles
import dev.geode.data.LfoStore
import dev.geode.data.prefs.PrefType
import dev.geode.data.prefs.prefKeys
import dev.geode.engine.bridge.CustomizeNative
import dev.geode.engine.bridge.ModulationNative
import dev.geode.engine.bridge.PanelNative
import dev.geode.model.SceneParams
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.update

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
 * to record history. The history, the rolls and the edits are native (core/viz/ParamHistory,
 * core/viz/PanelRoll, core/viz/ModDocument); this carries the look there and back.
 */
internal class ModulationController(
    private val lfoStore: LfoStore,
    private val prefs: SharedPreferences,
    private val host: Host,
    /** Hands the saved lock set to the director's keep set: `AutoDirectorStore.setKept`, its one writer. */
    private val pushKept: (Set<String>) -> Unit,
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

    fun toggleParamLock(label: String) {
        _lockedParams.update { if (label in it) it - label else it + label }
        prefs.edit().putStringSet(KEY_LOCKED_PARAMS, _lockedParams.value).apply()
        // The director's reach leaves a kept control alone too; its store pushes the set, here and at start.
        pushKept(_lockedParams.value)
    }

    /**
     * The single write path for a panel edit, so undo covers the whole panel. Native folds a slider
     * drag into one entry (core/viz/ParamHistory).
     */
    fun editSceneParams(params: SceneParams) = write(params, discrete = false)

    /** A discrete edit — a preset load, a randomize, a reset — always its own undo entry. */
    fun commitSceneParams(params: SceneParams) = write(params, discrete = true)

    fun undo() = step(redo = false)

    fun redo() = step(redo = true)

    private fun write(
        params: SceneParams,
        discrete: Boolean,
    ) {
        val previous = host.params
        if (params == previous) return
        _history.value =
            historyOf(PanelNative.paramHistoryRecord(previous.frame(), previous.customPaletteId, SystemClock.elapsedRealtime(), discrete))
        host.setSceneParams(params)
    }

    private fun step(redo: Boolean) {
        val current = host.params
        val out = FloatArray(SceneParams.FIELDS)
        val palette = PanelNative.paramHistoryStep(redo, current.frame(), current.customPaletteId, out) ?: return
        host.setSceneParams(SceneParams.read(out, palette))
        _history.value = historyOf(PanelNative.paramHistoryState())
    }

    /**
     * Rolls [category], or everything when null; [basicsOnly] is the roll without Pro. The roll, its seed,
     * the locks and whether the waves follow are native (core/viz/PanelRoll); this carries the frame there
     * and back. The knobs are not params: Quick knobs rolls their turns through MacroStore, and has no
     * roll here. True when the look changed.
     */
    fun randomizeParams(
        category: CustomizeCategory? = null,
        basicsOnly: Boolean = false,
    ): Boolean {
        if (category == CustomizeCategory.QUICK_KNOBS) return false
        val out = FloatArray(SceneParams.FIELDS)
        val bits = PanelNative.panelRoll(host.sceneId, category?.name, kept(), basicsOnly, host.params.frame(), out)
        return follow(bits, out)
    }

    /**
     * Randomize everything's roll (core/viz/RandomizeAll), after its style and preset are on screen: the
     * kept controls go back to their values in [before], then every category is rolled from [seed]. Native.
     */
    fun randomizeAllRoll(
        before: SceneParams,
        seed: Long,
        basicsOnly: Boolean,
    ) {
        val out = FloatArray(SceneParams.FIELDS)
        follow(PanelNative.panelRollAll(host.sceneId, kept(), basicsOnly, seed, before.frame(), host.params.frame(), out), out)
    }

    /** Randomize for the waves (core/viz/WaveRandomizer): [all] is the wave editor's button. Native. */
    fun randomizeWaves(all: Boolean) {
        val rolled = PanelNative.modWavesRoll(host.sceneId, kept(), all, host.params.frame(), _lfos.value) ?: return
        _lfos.value = rolled
        lfoStore.save(rolled)
    }

    /** Puts up what a roll [out] wrote and rolls the waves when native asks; true when the look changed. */
    private fun follow(
        bits: Int,
        out: FloatArray,
    ): Boolean {
        val changed = bits and PanelNative.PANEL_ROLL_CHANGED != 0
        if (changed) commitSceneParams(SceneParams.fromFrame(out, host.params))
        if (bits and PanelNative.PANEL_ROLL_WAVES != 0) randomizeWaves(all = false)
        return changed
    }

    private fun kept(): String = _lockedParams.value.joinToString("\n")

    /**
     * Puts one category's controls back to their defaults (native: every field its controls
     * write) and leaves every other category alone. Music's Reset also drops the user's own
     * reaction routes, which are not params.
     */
    fun resetCategory(category: CustomizeCategory) {
        if (category == CustomizeCategory.QUICK_KNOBS) return
        if (category == CustomizeCategory.MUSIC) editReactions(ModulationNative.MOD_EDIT_CLEAR)
        val out = FloatArray(SceneParams.FIELDS)
        CustomizeNative.customizeReset(category.name, host.params.frame(), out)
        commitSceneParams(SceneParams.fromFrame(out, host.params))
    }

    /** Takes one category's controls from another look ([from], a preset's params) and leaves the rest. Native. */
    fun borrowCategory(
        category: CustomizeCategory,
        from: SceneParams,
    ) {
        if (category == CustomizeCategory.QUICK_KNOBS) return
        val out = FloatArray(SceneParams.FIELDS)
        CustomizeNative.customizeBorrow(category.name, host.params.frame(), from.keepingInputsOf(host.params).frame(), out)
        commitSceneParams(SceneParams.fromFrame(out, host.params))
    }

    fun resetAll() = commitSceneParams(SceneParams.DEFAULT.keepingInputsOf(host.params))

    /** One [field] (a `ModulationNative.MOD_ADSR_*`) of the envelope at [index]; native sets it and keeps it in range. */
    fun setAdsr(
        index: Int,
        field: Int,
        value: Float,
    ) {
        val next = ModulationNative.modAdsrEdit(_adsrs.value, index, field, value)
        _adsrs.value = next
        lfoStore.saveAdsrs(next)
    }

    /** One [field] (a `ModulationNative.MOD_LFO_*`) of the wave at [index]; native sets it and keeps it in range. */
    fun setLfo(
        index: Int,
        field: Int,
        value: Float,
    ) {
        val next = ModulationNative.modLfoEdit(_lfos.value, index, field, value)
        _lfos.value = next
        lfoStore.save(next)
    }

    /**
     * One edit of the reactions (a `ModulationNative.MOD_EDIT_*` [op] with its [a], [b] and [value]). Native works
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
        val seed = host.params.int("reactSeed")
        val routes = ModulationNative.modReactionsEdit(host.sceneId, seed, _reactions.value, _handMask.value, op, a, b, value, outMask)
        val mask = outMask[0]
        _reactions.value = routes
        _handMask.value = mask
        lfoStore.pushHand(mask, routes != null)
        lfoStore.saveReactions(routes, mask)
    }

    /** A new personality for this style: the next DNA variant, and back to DNA if it was custom. */
    fun rerollReactions() {
        editReactions(ModulationNative.MOD_EDIT_CLEAR)
        val out = FloatArray(SceneParams.FIELDS)
        PanelNative.paramsRerollReactions(host.params.frame(), out)
        commitSceneParams(SceneParams.read(out, host.params.customPaletteId))
    }

    companion object {
        private fun historyOf(bits: Int) =
            ParamHistoryState(
                canUndo = bits and PanelNative.PARAM_HISTORY_UNDO != 0,
                canRedo = bits and PanelNative.PARAM_HISTORY_REDO != 0,
            )

        private const val KEY_LOCKED_PARAMS = "locked_params"

        val PREF_KEYS =
            prefKeys(GeodePrefsFiles.MODULATION) {
                portable(PrefType.STRING_SET, KEY_LOCKED_PARAMS)
            }
    }
}
