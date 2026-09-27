package dev.geode.nav

/** One step the [Navigator] took, narrated for whatever animates it. */
data class NavMove(
    val kind: Kind,
    val from: NavState,
    val to: NavState,
    /** Where the touch that caused this landed, when the caller knew: the natural origin for a ripple or an expanding surface. */
    val origin: Point? = null,
    /** True when a predictive-back drag drove it, so the surfaces are already part-way there. */
    val gesture: Boolean = false,
) {
    enum class Kind {
        PUSH,
        POP,
        REPLACE,
        SWITCH,
        OPEN_OVERLAY,
        CLOSE_OVERLAY,
        GATE_UP,
        GATE_DOWN,
    }
}
