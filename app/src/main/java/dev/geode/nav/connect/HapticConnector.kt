package dev.geode.nav.connect

/** The moments worth a haptic. The design system decides which get one and how it feels. */
enum class HapticCue {
    TAP,
    HOLD,
    SECTION,
    PAGE,
    SHEET,
    DISMISS,
    CONFIRM,
    REJECT,
}

fun interface HapticConnector {
    fun play(cue: HapticCue)

    companion object {
        val None = HapticConnector { }
    }
}
