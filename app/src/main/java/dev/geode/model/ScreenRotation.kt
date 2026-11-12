package dev.geode.model

/**
 * Settings › Look › Screen rotation, also on Visuals › Settings: whether the app turns with the
 * phone ([FOLLOW], the app before the choice existed) or stays in one orientation while the phone
 * moves. `MainActivity` hands it to the window; Android ignores it on screens 600 dp and wider.
 */
enum class ScreenRotation {
    FOLLOW,
    PORTRAIT,
    LANDSCAPE,
    ;

    companion object {
        fun fromName(name: String?): ScreenRotation = entries.firstOrNull { it.name == name } ?: FOLLOW
    }
}
