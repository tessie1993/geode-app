package dev.geode.render.scene

object MilkdropEngine {
    val available: Boolean =
        try {
            System.loadLibrary("milkdropjni")
            nativeGetLastError()
            true
        } catch (t: Throwable) {
            false
        }

    external fun nativeCreate(): Long

    external fun nativeDestroy(handle: Long)

    external fun nativeResize(
        handle: Long,
        width: Int,
        height: Int,
    )

    external fun nativeAddPcmMono(
        handle: Long,
        samples: FloatArray,
        count: Int,
    )

    external fun nativeRender(handle: Long)

    external fun nativeSetTexturePaths(
        handle: Long,
        dirs: Array<String>,
    )

    external fun nativeLoadPreset(
        handle: Long,
        path: String,
        smooth: Boolean,
    )

    external fun nativeGetLastError(): String?

    external fun nativeSetBeatSensitivity(
        handle: Long,
        value: Float,
    )

    external fun nativeSetPresetLocked(
        handle: Long,
        locked: Boolean,
    )
}
