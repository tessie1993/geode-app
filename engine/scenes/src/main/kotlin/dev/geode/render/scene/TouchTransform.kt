package dev.geode.render.scene

object TouchTransform {
    const val ZOOM_MIN = 0.3f
    const val ZOOM_MAX = 3f

    const val ROTATION_MIN = -3f
    const val ROTATION_MAX = 3f

    const val ROTATION_PER_DEGREE = 0.012f

    fun zoom(
        current: Float,
        zoomFactor: Float,
    ): Float {
        if (!zoomFactor.isFinite() || zoomFactor <= 0f) return current
        return (current * zoomFactor).coerceIn(ZOOM_MIN, ZOOM_MAX)
    }

    fun rotation(
        current: Float,
        degrees: Float,
    ): Float {
        if (!degrees.isFinite()) return current
        return (current + degrees * ROTATION_PER_DEGREE).coerceIn(ROTATION_MIN, ROTATION_MAX)
    }

    fun isTransform(
        zoomFactor: Float,
        degrees: Float,
    ): Boolean = (zoomFactor != 1f && zoomFactor > 0f) || degrees != 0f
}
