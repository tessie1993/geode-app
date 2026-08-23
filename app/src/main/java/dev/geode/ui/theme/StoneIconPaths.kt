package dev.geode.ui.theme

enum class StoneIcon {
    ADD,
    CHECK,
    CLOSE,
    DELETE,
    EDIT,
    FAVORITE,
    FILE,
    FOLDER,
    FULLSCREEN,
    LIBRARY,
    LYRICS,
    MICROPHONE,
    NEXT,
    PAUSE,
    PLAY,
    PREVIOUS,
    QUEUE,
    REPEAT,
    SEARCH,
    SETTINGS,
    SHARE,
    SHUFFLE,
    STUDIO,
    VISUALIZER,
}

internal data class StoneIconGlyph(
    val stroked: List<String> = emptyList(),
    val filled: List<String> = emptyList(),
)

internal const val STONE_ICON_VIEWPORT = 104f

internal val StoneIconGeometry: Map<StoneIcon, StoneIconGlyph> =
    mapOf(
        StoneIcon.ADD to
            StoneIconGlyph(
                stroked =
                    listOf(
                        "M52 22 V82 M22 52 H82",
                    ),
            ),
        StoneIcon.CHECK to
            StoneIconGlyph(
                stroked =
                    listOf(
                        "M24 54 L44 72 L81 30",
                    ),
            ),
        StoneIcon.CLOSE to
            StoneIconGlyph(
                stroked =
                    listOf(
                        "M26 26 L78 78 M78 26 L26 78",
                    ),
            ),
        StoneIcon.DELETE to
            StoneIconGlyph(
                stroked =
                    listOf(
                        "M31 31 H73 M40 31 V22 H64 V31 M36 31 L40 82 H65 L69 31 M47 44 V70 M58 44 V70",
                    ),
            ),
        StoneIcon.EDIT to
            StoneIconGlyph(
                stroked =
                    listOf(
                        "M25 75 L29 59 L65 23 L81 39 L45 75 Z M58 30 L74 46",
                    ),
            ),
        StoneIcon.FAVORITE to
            StoneIconGlyph(
                stroked =
                    listOf(
                        "M52 82 C43 70 22 59 22 39 C22 24 41 18 52 33 C63 18 82 24 82 39 C82 59 61 70 52 82 Z",
                    ),
            ),
        StoneIcon.FILE to
            StoneIconGlyph(
                stroked =
                    listOf(
                        "M30 18 H61 L77 34 V84 H30 Z M61 18 V34 H77",
                    ),
            ),
        StoneIcon.FOLDER to
            StoneIconGlyph(
                stroked =
                    listOf(
                        "M20 32 H44 L52 40 H84 V78 H20 Z",
                    ),
            ),
        StoneIcon.FULLSCREEN to
            StoneIconGlyph(
                stroked =
                    listOf(
                        "M20 40 V20 H40 M64 20 H84 V40 M84 64 V84 H64 M40 84 H20 V64",
                    ),
            ),
        StoneIcon.LIBRARY to
            StoneIconGlyph(
                stroked =
                    listOf(
                        "M25 24 V79 M40 24 V79 M57 28 L74 75 M22 79 H77",
                    ),
            ),
        StoneIcon.LYRICS to
            StoneIconGlyph(
                stroked =
                    listOf(
                        "M22 27 H82 V67 H48 L32 80 V67 H22 Z M34 41 H70 M34 53 H62",
                    ),
            ),
        StoneIcon.MICROPHONE to
            StoneIconGlyph(
                stroked =
                    listOf(
                        "M28 52 C28 73 76 73 76 52 M52 75 V86 M39 86 H65",
                        "M52 20H52A13 13 0 0 1 65 33V55A13 13 0 0 1 52 68H52A13 13 0 0 1 39 55V33A13 13 0 0 1 52 20Z",
                    ),
            ),
        StoneIcon.NEXT to
            StoneIconGlyph(
                filled =
                    listOf(
                        "M78 28 V76 M32 30 L66 52 L32 74 Z",
                    ),
            ),
        StoneIcon.PAUSE to
            StoneIconGlyph(
                stroked =
                    listOf(
                        "M34 28 V76 M66 28 V76",
                    ),
            ),
        StoneIcon.PLAY to
            StoneIconGlyph(
                filled =
                    listOf(
                        "M38 28 L78 52 L38 76 Z",
                    ),
            ),
        StoneIcon.PREVIOUS to
            StoneIconGlyph(
                filled =
                    listOf(
                        "M26 28 V76 M72 30 L38 52 L72 74 Z",
                    ),
            ),
        StoneIcon.QUEUE to
            StoneIconGlyph(
                stroked =
                    listOf(
                        "M24 31 H65 M24 51 H65 M24 71 H65 M76 56 V82 M65 71 L76 82 L87 71",
                    ),
            ),
        StoneIcon.REPEAT to
            StoneIconGlyph(
                stroked =
                    listOf(
                        "M25 40 C30 27 44 24 60 27 L74 31 M74 31 L67 23 M74 31 L65 37 " +
                            "M79 64 C73 77 58 80 43 77 L29 73 M29 73 L36 81 M29 73 L38 67",
                    ),
            ),
        StoneIcon.SEARCH to
            StoneIconGlyph(
                stroked =
                    listOf(
                        "M62 62 L82 82",
                        "M24 45a22 22 0 1 0 44 0a22 22 0 1 0 -44 0Z",
                    ),
            ),
        StoneIcon.SETTINGS to
            StoneIconGlyph(
                stroked =
                    listOf(
                        "M52 18 V27 M52 77 V86 M18 52 H27 M77 52 H86 M28 28 L34 34 M70 70 L76 76 M76 28 L70 34 M34 70 L28 76",
                        "M39 52a13 13 0 1 0 26 0a13 13 0 1 0 -26 0Z",
                    ),
            ),
        StoneIcon.SHARE to
            StoneIconGlyph(
                stroked =
                    listOf(
                        "M34 49 L65 31 M34 55 L65 73",
                        "M20 52a7 7 0 1 0 14 0a7 7 0 1 0 -14 0Z",
                        "M65 28a7 7 0 1 0 14 0a7 7 0 1 0 -14 0Z",
                        "M65 76a7 7 0 1 0 14 0a7 7 0 1 0 -14 0Z",
                    ),
            ),
        StoneIcon.SHUFFLE to
            StoneIconGlyph(
                stroked =
                    listOf(
                        "M20 34 H34 C48 34 54 70 70 70 H82 M70 70 L76 64 M70 70 L76 76 " +
                            "M20 70 H34 C45 70 54 34 70 34 H82 M70 34 L76 28 M70 34 L76 40",
                    ),
            ),
        StoneIcon.STUDIO to
            StoneIconGlyph(
                stroked =
                    listOf(
                        "M22 43 H82 M36 28 L46 43 M58 28 L68 43",
                        "M30 28H74A8 8 0 0 1 82 36V68A8 8 0 0 1 74 76H30A8 8 0 0 1 22 68V36A8 8 0 0 1 30 28Z",
                    ),
            ),
        StoneIcon.VISUALIZER to
            StoneIconGlyph(
                stroked =
                    listOf(
                        "M20 62 C28 24 37 80 46 43 C55 11 65 82 82 35",
                    ),
            ),
    )
