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

    /**
     * The media notification's extra buttons (core/shell/Behaviour). A button is its ordinal (shuffle, repeat,
     * favourite), a set of them the mask of `1 shl ordinal`. The [chosen] buttons in the order the session
     * offers them (`geode_notification_buttons_ordered`).
     */
    external fun notificationButtonsOrdered(chosen: Int): IntArray

    /** [chosen] with [button] switched; switching one on past the room drops the last other one (`geode_notification_buttons_toggled`). */
    external fun notificationButtonsToggled(
        chosen: Int,
        button: Int,
        on: Boolean,
    ): Int

    /** The last screen to keep, by ordinal: "last used" reads as the player (`geode_behaviour_last_screen`). */
    external fun behaviourLastScreen(last: Int): Int

    /** The screen to open on, by ordinal: [start], or [last] when [start] is "last used" (`geode_behaviour_opens_on`). */
    external fun behaviourOpensOn(
        start: Int,
        last: Int,
    ): Int
}
