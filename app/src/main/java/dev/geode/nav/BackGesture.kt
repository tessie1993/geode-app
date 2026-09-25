package dev.geode.nav

/**
 * A predictive-back drag in flight. [progress] runs 0–1 with the finger and [target] is where
 * letting go lands, so the surface that leaves and the one that arrives can move together
 * before anything is committed.
 */
data class BackGesture(
    val progress: Float,
    val edge: Edge,
    val target: NavState,
) {
    enum class Edge {
        LEFT,
        RIGHT,
    }
}
