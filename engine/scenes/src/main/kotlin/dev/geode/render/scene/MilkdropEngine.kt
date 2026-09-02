package dev.geode.render.scene

/** MilkDrop lives inside `libgeode.so` now; it is available exactly when the engine library loads. */
object MilkdropEngine {
    val available: Boolean =
        try {
            System.loadLibrary("geode")
            true
        } catch (t: Throwable) {
            false
        }
}
