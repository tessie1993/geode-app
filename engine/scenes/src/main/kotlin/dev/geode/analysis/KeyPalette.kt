package dev.geode.analysis

object KeyPalette {
    private val CIRCLE_OF_FIFTHS =
        listOf("C", "G", "D", "A", "E", "B", "F#", "C#", "G#", "D#", "A#", "F")

    private val CHROMATIC =
        listOf("C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B")

    private const val RELATIVE_MAJOR_SEMITONES = 3

    private val ENHARMONIC =
        mapOf(
            "DB" to "C#",
            "EB" to "D#",
            "GB" to "F#",
            "AB" to "G#",
            "BB" to "A#",
            "CB" to "B",
            "FB" to "E",
            "E#" to "F",
            "B#" to "C",
        )

    private const val MINOR_OFFSET = 1f / 12f * 0.2f

    fun hueFor(key: String): Float? {
        val trimmed = key.trim()
        if (trimmed.isEmpty()) return null
        val minor = trimmed.contains("minor", ignoreCase = true)
        val spelled = trimmed.substringBefore(' ').uppercase().let { ENHARMONIC[it] ?: it }
        val root =
            if (minor) {
                val at = CHROMATIC.indexOf(spelled)
                if (at < 0) return null
                CHROMATIC[(at + RELATIVE_MAJOR_SEMITONES) % CHROMATIC.size]
            } else {
                spelled
            }
        val at = CIRCLE_OF_FIFTHS.indexOf(root)
        if (at < 0) return null
        val hue = at.toFloat() / CIRCLE_OF_FIFTHS.size + if (minor) MINOR_OFFSET else 0f
        return hue - kotlin.math.floor(hue)
    }

    fun areNeighbours(
        a: String,
        b: String,
    ): Boolean {
        val ai = CIRCLE_OF_FIFTHS.indexOf(a.trim().substringBefore(' ').uppercase())
        val bi = CIRCLE_OF_FIFTHS.indexOf(b.trim().substringBefore(' ').uppercase())
        if (ai < 0 || bi < 0) return false
        val gap = kotlin.math.abs(ai - bi)
        return gap == 1 || gap == CIRCLE_OF_FIFTHS.size - 1
    }

    fun hueDistance(
        a: Float,
        b: Float,
    ): Float {
        val d = kotlin.math.abs(a - b)
        return minOf(d, 1f - d)
    }
}
