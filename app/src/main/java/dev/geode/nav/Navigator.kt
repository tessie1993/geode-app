package dev.geode.nav

import kotlinx.coroutines.channels.BufferOverflow
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update

/**
 * The one object that knows where the person is and moves them. Pure state: it holds no views,
 * imports nothing from Compose or Android, and can be driven from a unit test.
 *
 * [state] is the current [NavState]. [moves] narrates each change for the transition connector.
 * [backGesture] follows a predictive-back drag so a surface can move with the finger before the
 * pop is committed. Everything here expects the main thread.
 */
class Navigator(
    initial: NavState = NavState(),
    initialLink: DeepLink? = null,
) {
    private val _state = MutableStateFlow(initial)
    val state: StateFlow<NavState> = _state.asStateFlow()

    private val _moves = MutableSharedFlow<NavMove>(extraBufferCapacity = MOVE_BUFFER, onBufferOverflow = BufferOverflow.DROP_OLDEST)
    val moves: SharedFlow<NavMove> = _moves.asSharedFlow()

    private val _backGesture = MutableStateFlow<BackGesture?>(null)
    val backGesture: StateFlow<BackGesture?> = _backGesture.asStateFlow()

    private val _pendingLink = MutableStateFlow(initialLink)

    /** The last deep link routed, held until whatever imports or plays it calls [consumeLink]. */
    val pendingLink: StateFlow<DeepLink?> = _pendingLink.asStateFlow()

    private val now: NavState get() = _state.value

    /**
     * Goes to [destination]: a page or sheet is pushed, a root replaces its section's stack, and
     * a destination in another section switches there first. Pushing what is already on top does
     * nothing.
     */
    fun go(
        destination: Destination,
        origin: Point? = null,
    ) {
        val from = now
        val section = destination.section ?: from.section
        val stack = from.stacks.getValue(section)
        val next =
            when {
                destination.presentation == Presentation.ROOT -> listOf(destination)
                stack.last() == destination -> stack
                else -> stack + destination
            }
        if (section == from.section && next == stack) return
        val kind =
            when {
                section != from.section -> NavMove.Kind.SWITCH
                destination.presentation == Presentation.ROOT -> NavMove.Kind.REPLACE
                else -> NavMove.Kind.PUSH
            }
        commit(kind, from.copy(section = section, stacks = from.stacks + (section to next)), origin)
    }

    /** Shows [section] where it was last left; showing the current one again pops it to its root. */
    fun show(
        section: Section,
        origin: Point? = null,
    ) {
        val from = now
        if (from.section == section) {
            popToRoot(origin)
            return
        }
        commit(NavMove.Kind.SWITCH, from.copy(section = section), origin)
    }

    /**
     * Swaps the top of the current section for [destination]; back then skips what was there. The
     * root is never swapped for a page, so at the root a page is pushed instead.
     */
    fun replace(
        destination: Destination,
        origin: Point? = null,
    ) {
        val from = now
        if (destination.presentation == Presentation.ROOT) {
            go(destination, origin)
            return
        }
        val destinationSection = destination.section
        if (destinationSection != null && destinationSection != from.section) {
            val section = destinationSection
            val stack = from.stacks.getValue(section)
            val base = if (stack.size > 1) stack.dropLast(1) else stack
            val next = base + destination
            commit(
                NavMove.Kind.SWITCH,
                from.copy(section = section, stacks = from.stacks + (section to next)),
                origin,
            )
            return
        }
        val keepsRoot = from.stack.size > 1 || destination.presentation == Presentation.ROOT
        val base = if (keepsRoot) from.stack.dropLast(1) else from.stack
        commit(NavMove.Kind.REPLACE, from.copy(stacks = from.stacks + (from.section to base + destination)), origin)
    }

    fun popToRoot(origin: Point? = null) {
        val from = now
        if (from.stack.size == 1) return
        commit(NavMove.Kind.POP, from.copy(stacks = from.stacks + (from.section to listOf(from.stack.first()))), origin)
    }

    /** Pops the current section until [destination] is on top; nothing happens if it is not in the stack. */
    fun popTo(destination: Destination) {
        val from = now
        val index = from.stack.lastIndexOf(destination)
        if (index < 0 || index == from.stack.lastIndex) return
        commit(NavMove.Kind.POP, from.copy(stacks = from.stacks + (from.section to from.stack.take(index + 1))))
    }

    fun open(
        overlay: Overlay,
        origin: Point? = null,
    ) {
        val from = now
        if (overlay in from.overlays) return
        commit(NavMove.Kind.OPEN_OVERLAY, from.copy(overlays = from.overlays + overlay), origin)
    }

    fun close(overlay: Overlay) {
        val from = now
        if (overlay !in from.overlays) return
        commit(NavMove.Kind.CLOSE_OVERLAY, from.copy(overlays = from.overlays - overlay))
    }

    /** Puts [gate] over the shell, replacing any gate already up. */
    fun raiseGate(gate: Gate) {
        val from = now
        if (from.gate == gate) return
        commit(NavMove.Kind.GATE_UP, from.copy(gate = gate))
    }

    /** Takes [gate] down if it is the one up; a stale call for a gate already gone is ignored. */
    fun clearGate(gate: Gate) {
        val from = now
        if (from.gate != gate) return
        commit(NavMove.Kind.GATE_DOWN, from.copy(gate = null))
    }

    /**
     * What back does, in order: dismisses a dismissible gate, closes the top overlay, pops the
     * current section, returns to Player from any other section. False when there is nothing
     * left to do, so the caller lets the system finish — leave the app.
     *
     * If a predictive-back drag is in flight this commits it, and the move is flagged as
     * gesture-driven so the transition can continue from where the finger left the surfaces.
     */
    fun back(): Boolean {
        val gesture = _backGesture.value
        _backGesture.value = null
        val from = now
        val to = gesture?.target ?: backTarget(from) ?: return false
        commit(backKind(from, to), to, gesture = gesture != null)
        return true
    }

    fun backStarted(edge: BackGesture.Edge) {
        val target = backTarget(now) ?: return
        _backGesture.value = BackGesture(progress = 0f, edge = edge, target = target)
    }

    fun backProgressed(progress: Float) {
        _backGesture.update { it?.copy(progress = progress.coerceIn(0f, 1f)) }
    }

    fun backCancelled() {
        _backGesture.value = null
    }

    /** Sends [link] to the surface that handles it and holds it in [pendingLink] until consumed. */
    fun handle(link: DeepLink) {
        _pendingLink.value = link
        when (link) {
            is DeepLink.Preset -> go(Destination.Visuals.Presets)
            is DeepLink.Template -> go(Destination.Studio.Templates)
            is DeepLink.PlayFromSearch -> show(Section.PLAYER)
        }
    }

    fun consumeLink(link: DeepLink) {
        _pendingLink.compareAndSet(link, null)
    }

    private fun backTarget(from: NavState): NavState? =
        when {
            // A non-dismissible gate owns back too; never let a gesture dismiss content behind it.
            from.gate != null -> if (from.gate.dismissible) from.copy(gate = null) else null
            from.overlays.isNotEmpty() -> from.copy(overlays = from.overlays.dropLast(1))
            from.stack.size > 1 -> from.copy(stacks = from.stacks + (from.section to from.stack.dropLast(1)))
            from.section != Section.PLAYER -> from.copy(section = Section.PLAYER)
            else -> null
        }

    private fun backKind(
        from: NavState,
        to: NavState,
    ): NavMove.Kind =
        when {
            from.gate != to.gate -> NavMove.Kind.GATE_DOWN
            from.overlays.size != to.overlays.size -> NavMove.Kind.CLOSE_OVERLAY
            from.section != to.section -> NavMove.Kind.SWITCH
            else -> NavMove.Kind.POP
        }

    private fun commit(
        kind: NavMove.Kind,
        to: NavState,
        origin: Point? = null,
        gesture: Boolean = false,
    ) {
        val from = now
        if (to == from) return
        if (!gesture) _backGesture.value = null
        _state.value = to
        _moves.tryEmit(NavMove(kind, from, to, origin, gesture))
    }

    private companion object {
        /** Moves are consumed on the next frame; a burst beyond this drops the oldest, which the state already reflects. */
        const val MOVE_BUFFER = 16
    }
}
