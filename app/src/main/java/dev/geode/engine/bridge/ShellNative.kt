package dev.geode.engine.bridge

// The JNI declarations of geode_shell_jni.cpp (CLAUDE.md).
object ShellNative {
    init {
        GeodeNative.ensureLoaded()
    }

    // The shell's rules (core/shell through core/api/geode_shell_api.h).

    /** The frame rates an export offers, ascending (`geode_export_fps_options`). */
    external fun exportFpsOptions(): IntArray

    /** A stored frame rate kept when it is offered, else [fallback] (`geode_export_fps_read`). */
    external fun exportFpsRead(
        stored: Int,
        fallback: Int,
    ): Int
}
