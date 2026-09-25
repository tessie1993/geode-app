package dev.geode.render

/** Packs live surface-space pointers into y-up NDC, excluding the pointer leaving this event. */
internal inline fun packTouchPoints(
    pointerCount: Int,
    leavingPointer: Int,
    width: Int,
    height: Int,
    output: FloatArray,
    xAt: (Int) -> Float,
    yAt: (Int) -> Float,
): Int {
    if (width <= 0 || height <= 0) return 0
    var live = 0
    val capacity = minOf(TouchField.MAX_POINTS, output.size / 2)
    for (i in 0 until pointerCount) {
        if (i == leavingPointer) continue
        if (live == capacity) break
        output[live * 2] = (xAt(i) / width * 2f - 1f).coerceIn(-1f, 1f)
        output[live * 2 + 1] = (1f - yAt(i) / height * 2f).coerceIn(-1f, 1f)
        live++
    }
    return live
}
