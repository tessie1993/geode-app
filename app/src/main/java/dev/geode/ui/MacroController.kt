package dev.geode.ui

import dev.geode.data.MacroSetStore
import dev.geode.data.SavedMacroSet
import dev.geode.render.ReactionRoute
import dev.geode.render.scene.BuildStyleReactionsUseCase
import dev.geode.render.scene.MacroKnobs
import dev.geode.render.scene.MacroSet
import dev.geode.render.scene.SceneParams
import dev.geode.render.scene.StandardMacros
import dev.geode.render.scene.UserMacro
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlin.random.Random

/**
 * The macro knobs: the active [MacroSet], turning a knob, and keeping its sounds in the matrix.
 *
 * A turn writes the real parameters through [Host.editSceneParams], the panel's one undoable
 * write path, so the sliders follow and a drag is one undo entry. Each drag is applied to the
 * params it started from ([MacroKnobs.turn]); the anchor holds while the live params are still
 * the ones this controller last wrote, and any other edit (a slider, undo, a preset) starts a
 * fresh one.
 *
 * A knob's sounds are played as reaction routes in the user's own route list, so they run in
 * the live view, the wallpaper and exports exactly like any other reaction, and they are taken
 * back out when the set changes.
 */
internal class MacroController(
    val store: MacroSetStore,
    private val host: Host,
) {
    interface Host {
        val params: SceneParams

        val sceneId: String

        val reactions: List<ReactionRoute>?

        fun setReactions(routes: List<ReactionRoute>?)

        fun editSceneParams(params: SceneParams)

        fun commitSceneParams(params: SceneParams)
    }

    private val _set = MutableStateFlow(store.loadActive())
    val set: StateFlow<MacroSet> = _set

    private var anchor: SceneParams? = null
    private var anchorIndex = -1
    private var anchorValue = 0f
    private var lastWritten: SceneParams? = null

    fun turn(
        index: Int,
        to: Float,
    ) {
        val current = _set.value
        val macro = current.macros.getOrNull(index) ?: return
        val from = anchor?.takeIf { anchorIndex == index && lastWritten == host.params }
        if (from == null) {
            anchor = host.params
            anchorIndex = index
            anchorValue = macro.value
        }
        val out = MacroKnobs.turn(checkNotNull(anchor), macro, anchorValue, to, host.sceneId)
        lastWritten = out
        publish(current.withValue(index, to.coerceIn(0f, 1f)))
        host.editSceneParams(out)
    }

    /** Every knob somewhere new, away from the extremes; one undo entry for the params. */
    fun surprise(rng: Random = Random.Default) {
        val current = _set.value
        var params = host.params
        val turned =
            current.macros.map { m ->
                val to = if (m.bipolar) SURPRISE_LOW + rng.nextFloat() * SURPRISE_SPAN else rng.nextFloat() * SURPRISE_UNIPOLAR
                params = MacroKnobs.turn(params, m, m.value, to, host.sceneId)
                m.copy(value = to)
            }
        settle(current.copy(macros = turned), params)
    }

    /** Every knob back to its rest, taking its destinations back with it; one undo entry. */
    fun resetKnobs() {
        val current = _set.value
        var params = host.params
        val rested =
            current.macros.map { m ->
                params = MacroKnobs.turn(params, m, m.value, m.rest, host.sceneId)
                m.copy(value = m.rest)
            }
        settle(current.copy(macros = rested), params)
    }

    /**
     * Makes [next] the active set (an editor change, a set loaded from the folder, the standard
     * six). Knob positions are kept as [next] has them; the params are not touched, because a
     * set describes how knobs move things, not a look. Sounds are swapped in the route list.
     */
    fun setActive(next: MacroSet) {
        val previous = _set.value
        syncSounds(previous, next)
        anchor = null
        publish(next)
    }

    fun restoreStandard() = setActive(StandardMacros.SET)

    private fun settle(
        next: MacroSet,
        params: SceneParams,
    ) {
        anchor = null
        publish(next)
        host.commitSceneParams(params)
    }

    private fun publish(next: MacroSet) {
        _set.value = next
        store.saveActive(next)
    }

    private fun syncSounds(
        previous: MacroSet,
        next: MacroSet,
    ) {
        val old = previous.macros.flatMap { MacroKnobs.soundRoutes(it, host.sceneId) }
        val new = next.macros.flatMap { MacroKnobs.soundRoutes(it, host.sceneId) }
        if (old == new) return
        val base = host.reactions ?: BuildStyleReactionsUseCase()(host.sceneId, host.params.reactSeed)
        val kept = base.filterNot { r -> old.any { it.sameSlot(r) } || new.any { it.sameSlot(r) } }
        host.setReactions((kept + new).take(ReactionRoute.MAX_ROUTES))
    }

    private fun MacroSet.withValue(
        index: Int,
        value: Float,
    ): MacroSet = copy(macros = macros.mapIndexed { i, m -> if (i == index) m.copy(value = value) else m })

    private fun ReactionRoute.sameSlot(other: ReactionRoute) = source == other.source && target == other.target

    private companion object {
        const val SURPRISE_LOW = 0.2f
        const val SURPRISE_SPAN = 0.6f
        const val SURPRISE_UNIPOLAR = 0.55f
    }
}

/** The knob a new macro starts as in the editor. */
internal fun newMacro(index: Int): UserMacro = UserMacro(name = "Macro ${index + 1}")

/** What the editor needs from the session: the set's folder and the look to record from. */
internal class MacroEditorActions(
    val saved: List<SavedMacroSet>,
    val onSave: (folder: String, done: (Boolean) -> Unit) -> Unit,
    val onLoad: (SavedMacroSet, done: (Boolean) -> Unit) -> Unit,
    val onDelete: (SavedMacroSet) -> Unit,
    val onImport: (String) -> Boolean,
    val onRestoreStandard: () -> Unit,
)
