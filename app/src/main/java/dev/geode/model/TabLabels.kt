package dev.geode.model

/**
 * Which tab bar items draw their name under the icon (Settings › Look › Layout). [AUTO] is the
 * bar as it was before the choice existed: every label while there is room, only the selected
 * one on a narrow window or at a large font (#558).
 */
enum class TabLabels {
    AUTO,
    ALWAYS,
    SELECTED,
    NEVER,
    ;

    /** Whether an item draws its label; [roomIsShort] is the bar's own narrow-or-large-font test. */
    fun shows(
        selected: Boolean,
        roomIsShort: Boolean,
    ): Boolean =
        when (this) {
            AUTO -> !roomIsShort || selected
            ALWAYS -> true
            SELECTED -> selected
            NEVER -> false
        }

    companion object {
        fun fromName(name: String?): TabLabels = entries.firstOrNull { it.name == name } ?: AUTO
    }
}
