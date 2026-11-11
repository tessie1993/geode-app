package dev.geode.model

/** How big Quick knobs' dials are drawn (Settings › Look › Layout), in the drawer and in Customize. [LARGE] is the size they first shipped at. */
enum class KnobSize {
    SMALL,
    MEDIUM,
    LARGE,
    ;

    companion object {
        fun fromName(name: String?): KnobSize = entries.firstOrNull { it.name == name } ?: SMALL
    }
}
