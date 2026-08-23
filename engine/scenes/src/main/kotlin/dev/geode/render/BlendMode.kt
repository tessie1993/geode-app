package dev.geode.render

enum class BlendMode {
    NORMAL,

    SCREEN,

    ADD,

    MULTIPLY,

    DIFFERENCE,

    OVERLAY,

    LIGHTEN,

    DARKEN,
    ;

    companion object {
        fun fromOrdinal(i: Int): BlendMode = entries.getOrElse(i) { SCREEN }
    }
}
