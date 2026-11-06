package dev.geode.ui

/**
 * The route the user took through a section, as a list of keys from the root to what is on
 * screen, so back retraces it instead of jumping to the root. Pure Kotlin over a [MutableList]:
 * hand it a `SnapshotStateList` and Compose redraws on every change, hand it a plain list and a
 * JVM test can drive it. Visuals uses it today ([VisualsNavState]); Studio is meant to reuse it
 * the same way, with its own key type.
 *
 * Three moves, and only these, change the route:
 * - [open] goes forward. A key that is already on the route is not added twice: the route is cut
 *   back to it, so back never loops (Styles → Customize → Presets → Customize, then back, goes to
 *   Styles, not to Presets and Customize again) and the route can never grow past the number of
 *   keys there are. A Navigation 3 back stack also needs each key once, since entries are told
 *   apart by key.
 * - [step] goes sideways: it swaps what is on top for its neighbour (Advanced's ‹ › arrows), so
 *   paging through ten groups costs one back, not ten.
 * - [pop] goes back one, and never past the root, which stays put: the caller leaves the section
 *   once [canPop] is false.
 */
internal class NavHistory<K : Any>(
    private val route: MutableList<K>,
) {
    init {
        require(route.isNotEmpty()) { "A route starts at its root" }
    }

    val root: K get() = route.first()

    val top: K get() = route.last()

    /** The route from the root to [top]; a live view, not a copy. */
    val keys: List<K> get() = route

    val canPop: Boolean get() = route.size > 1

    operator fun contains(key: K): Boolean = key in route

    /** Forward to [key], or back to it when it is already on the route (the root included). */
    fun open(key: K) {
        val at = route.indexOf(key)
        if (at >= 0) truncateTo(at) else route.add(key)
    }

    /** Back one; false at the root, where back belongs to whoever holds this history. */
    fun pop(): Boolean {
        if (!canPop) return false
        route.removeAt(route.lastIndex)
        return true
    }

    /**
     * Sideways: [top] becomes its neighbour [delta] places along [order], adding no back entry.
     * The ends do not wrap (the arrow there is simply off), and a top that is not in [order] does
     * not step, nor does a neighbour already further down the route. True when it stepped.
     */
    fun step(
        order: List<K>,
        delta: Int,
    ): Boolean {
        val next = neighbour(order, delta) ?: return false
        route[route.lastIndex] = next
        return true
    }

    /** Where [step] would land, or null when it would not move; for enabling the arrows. */
    fun neighbour(
        order: List<K>,
        delta: Int,
    ): K? {
        if (!canPop || delta == 0) return null
        val at = order.indexOf(top)
        if (at < 0) return null
        val next = order.getOrNull(at + delta) ?: return null
        return next.takeIf { it !in route }
    }

    private fun truncateTo(index: Int) {
        while (route.lastIndex > index) route.removeAt(route.lastIndex)
    }
}
