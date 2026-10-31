package dev.geode.render.scene

/** A pinch scales Zoom and a twist turns Rotation, within the native catalogue's ranges. */
object TouchTransform {
    fun zoom(
        current: Float,
        zoomFactor: Float,
    ): Float {
        if (!zoomFactor.isFinite() || zoomFactor <= 0f) return current
        return (current * zoomFactor).coerceIn(Catalogue.range(ParamKeys.ZOOM))
    }

    fun rotation(
        current: Float,
        degrees: Float,
    ): Float {
        if (!degrees.isFinite()) return current
        return (current + degrees * Catalogue.value("touchRotationPerDegree")).coerceIn(Catalogue.range(ParamKeys.ROTATION))
    }

    fun isTransform(
        zoomFactor: Float,
        degrees: Float,
    ): Boolean = (zoomFactor != 1f && zoomFactor > 0f) || degrees != 0f
}
