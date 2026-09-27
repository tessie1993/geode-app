package dev.geode.nav

/** A position in root-relative pixels: where a touch landed, where a ripple starts. */
data class Point(
    val x: Float,
    val y: Float,
) {
    companion object {
        val ZERO = Point(0f, 0f)
    }
}
