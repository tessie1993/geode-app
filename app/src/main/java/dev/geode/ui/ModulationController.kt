package dev.geode.ui

import android.os.SystemClock
import dev.geode.data.KeepSetStore
import dev.geode.data.LfoStore
import dev.geode.engine.bridge.CustomizeNative
import dev.geode.engine.bridge.ModulationNative
import dev.geode.engine.bridge.PanelNative
import dev.geode.engine.bridge.ResponderNative
import dev.geode.model.SceneParams
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.FlowPreview
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.debounce
import kotlinx.coroutines.flow.drop
import kotlinx.coroutines.launch

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
 * to record history. The history and the rolls are native (core/viz/ParamHistory, core/viz/PanelRoll);
 * this carries the look there and back while the look itself is still the shell's (VizStateStore).
 *
 * The modulation (the waves, the envelopes, the reaction routes with their hand mask) and the keep set
 * are native's (core/viz/ModState): an edit here is an event sent there that returns nothing, and native
 * applies it with every rule. [lfos], [adsrs], [reactions] and [handMask] are read-only mirrors of what
 * native holds, read back after each event for the editors and the renderer's look.
 */
@OptIn(FlowPreview::class)
internal class ModulationController(
    private val lfoStore: LfoStore,
    /** Customize's keep set: native holds it and applies a tap (core/viz/ModState); the store saves and shows it. */
    private val keepSet: KeepSetStore,
    private val host: Host,
    scope: CoroutineScope,
) {
    interface Host {
        val params: SceneParams

        val sceneId: String

        fun setSceneParams(params: SceneParams)
    }

    // What was saved goes to native once; native pushes the hand mask process-wide, where every renderer merges
    // the user's routes with the live DNA and every director keeps off the moments they own.
    init {
        ModulationNative.modStateLoad(lfoStore.load(), lfoStore.loadAdsrs(), lfoStore.loadReactions(), lfoStore.loadHandMask())
    }

    private val _lfos = MutableStateFlow(held(ModulationNative.MOD_STATE_LFOS).orEmpty())
    val lfos: StateFlow<String> = _lfos

    private val _adsrs = MutableStateFlow(held(ModulationNative.MOD_STATE_ADSRS).orEmpty())
    val adsrs: StateFlow<String> = _adsrs

    // Null while the style's DNA plays; the first edit makes native copy the DNA into a list of the user's own.
    private val _reactions = MutableStateFlow(held(ModulationNative.MOD_STATE_REACTIONS))
    val reactions: StateFlow<String?> = _reactions

    // Which moments that list owns (Customize › Music: the hand wins per moment), saved beside it.
    private val _handMask = MutableStateFlow(ModulationNative.modStateHandMask())
    val handMask: StateFlow<Int> = _handMask

    init {
        // A drag sends an event per frame: the prefs are written once it settles, not per event.
        scope.launch { _lfos.drop(1).debounce(SAVE_QUIET_MS).collect { lfoStore.save(it) } }
        scope.launch { _adsrs.drop(1).debounce(SAVE_QUIET_MS).collect { lfoStore.saveAdsrs(it) } }
    }

    val lockedParams: StateFlow<Set<String>> get() = keepSet.keys

    private val _history = MutableStateFlow(ParamHistoryState())
    val history: StateFlow<ParamHistoryState> = _history

    /** A Keep tap: native toggles [label] in the set it holds and hands the director the new set. */
    fun toggleParamLock(label: String) = keepSet.tap(label)

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
        val now = SystemClock.elapsedRealtime()
        _history.value = historyOf(PanelNative.paramHistoryRecord(previous.frame(), previous.customPaletteId, host.sceneId, now, discrete))
        host.setSceneParams(params)
    }

    private fun step(redo: Boolean) {
        val current = host.params
        val out = FloatArray(SceneParams.FIELDS)
        val palette = PanelNative.paramHistoryStep(redo, current.frame(), current.customPaletteId, host.sceneId, out) ?: return
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
        PanelNative.modStateWavesRoll(host.sceneId, all, host.params.frame())
        _lfos.value = held(ModulationNative.MOD_STATE_LFOS).orEmpty()
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

    private fun kept(): String = keepSet.document.value

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

    /** One [field] (a `ModulationNative.MOD_ADSR_*`) of the envelope at [index]: an event; native sets it and keeps it in range. */
    fun setAdsr(
        index: Int,
        field: Int,
        value: Float,
    ) {
        ModulationNative.modStateAdsrSet(index, field, value)
        _adsrs.value = held(ModulationNative.MOD_STATE_ADSRS).orEmpty()
    }

    /** One [field] (a `ModulationNative.MOD_LFO_*`) of the wave at [index]: an event; native sets it and keeps it in range. */
    fun setLfo(
        index: Int,
        field: Int,
        value: Float,
    ) {
        ModulationNative.modStateLfoSet(index, field, value)
        _lfos.value = held(ModulationNative.MOD_STATE_LFOS).orEmpty()
    }

    /**
     * One edit of the reactions (a `ModulationNative.MOD_EDIT_*` [op] with its [a], [b] and [value]): an event.
     * Native works out the user's routes and the moments they own from what plays on this style, hands the
     * matrix back to the style's DNA when no route is the user's, and pushes the hand mask. A tap: saved at once.
     */
    fun editReactions(
        op: Int,
        a: Int = 0,
        b: Int = 0,
        value: Float = 0f,
    ) {
        ModulationNative.modStateReactionsEdit(host.sceneId, host.params.int("reactSeed"), op, a, b, value)
        val routes = held(ModulationNative.MOD_STATE_REACTIONS)
        val mask = ModulationNative.modStateHandMask()
        _reactions.value = routes
        _handMask.value = mask
        lfoStore.saveReactions(routes, mask)
    }

    /**
     * One edit of the Event Responder's rows and responses ([ResponderNative.RESP_EDIT_ROW_ON] and the rest): an event.
     * Native edits the saved text and hands the new one back; it goes where an edit of the routes goes.
     */
    fun editResponder(
        op: Int,
        row: Int,
        index: Int = 0,
        field: Int = 0,
        value: Float = 0f,
    ) {
        ResponderNative.responderEdit(_reactions.value, op, row, index, field, value)?.let(::takeReactions)
    }

    /** One edit of the Song events catalogue ([ResponderNative.RESP_CATALOG_ADD] and the rest), as [editResponder]. */
    fun editResponderCatalog(
        op: Int,
        row: Int,
        index: Int = 0,
        value: Float = 0f,
    ) = takeReactions(ResponderNative.responderCatalogEdit(_reactions.value, op, row, index, value))

    /** Puts back the text the reactions had (a row's Undo): the same path as an edit. Null is the style's own. */
    fun restoreReactions(text: String?) = takeReactions(text)

    /**
     * Keeps [text] as the user's reactions: native reads the hand mask it holds, holds the text with the waves and
     * envelopes, and what it holds is what the editors, the renderer's look and the prefs get. A text that is no
     * list (native says -1) changes nothing.
     */
    private fun takeReactions(text: String?) {
        if (text != null && text.isEmpty()) return
        val mask = if (text == null) 0 else ModulationNative.modHandLoad(text, true, _handMask.value)
        if (mask < 0) return
        ModulationNative.modStateLoad(
            held(ModulationNative.MOD_STATE_LFOS).orEmpty(),
            held(ModulationNative.MOD_STATE_ADSRS).orEmpty(),
            text,
            mask,
        )
        val routes = held(ModulationNative.MOD_STATE_REACTIONS)
        val held = ModulationNative.modStateHandMask()
        _reactions.value = routes
        _handMask.value = held
        lfoStore.saveReactions(routes, held)
    }

    /** A new personality for this style: the next DNA variant, and back to DNA if it was custom. */
    fun rerollReactions() {
        editReactions(ModulationNative.MOD_EDIT_CLEAR)
        val out = FloatArray(SceneParams.FIELDS)
        PanelNative.paramsRerollReactions(host.params.frame(), out)
        commitSceneParams(SceneParams.read(out, host.params.customPaletteId))
    }

    private fun held(which: Int): String? = ModulationNative.modStateText(which)

    companion object {
        /** How long a wave or envelope drag rests before what native holds is saved. */
        private const val SAVE_QUIET_MS = 400L

        private fun historyOf(bits: Int) =
            ParamHistoryState(
                canUndo = bits and PanelNative.PARAM_HISTORY_UNDO != 0,
                canRedo = bits and PanelNative.PARAM_HISTORY_REDO != 0,
            )
    }
}
