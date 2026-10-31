package dev.geode.render

import org.junit.Assert.assertEquals
import org.junit.Test

class ParamMatrixCatalogTest {
    @Test
    fun parsesSourcesAndTargetsInWireOrder() {
        val c =
            ParamMatrixCatalog.parse(
                "S|BASS|Bass|LEVELS|0|soft|loud|FOLLOW|0.03|0.25\nS|KICK|Kick hit|DRUMS|1|between kicks|on the kick|FOLLOW|0.01|0.18\n" +
                    "T|NONE|None|NONE|0|0|0|0|-\nT|ROTATION|Rotation|MOTION|0|-3.0|3.0|1.5|BASS_TIME\n",
            )
        assertEquals(
            listOf(
                ReactSourceInfo(0, "BASS", "Bass", "LEVELS", false, "soft", "loud", ReactMode.FOLLOW, 0.03f, 0.25f),
                ReactSourceInfo(1, "KICK", "Kick hit", "DRUMS", true, "between kicks", "on the kick", ReactMode.FOLLOW, 0.01f, 0.18f),
            ),
            c.sources,
        )
        assertEquals(
            listOf(
                ReactTargetInfo(0, "NONE", "None", "NONE", false, 0f, 0f, 0f, null),
                ReactTargetInfo(1, "ROTATION", "Rotation", "MOTION", false, -3f, 3f, 1.5f, "BASS_TIME"),
            ),
            c.targets,
        )
    }

    @Test
    fun aMalformedLineKeepsTheIdsOfTheRest() {
        val c = ParamMatrixCatalog.parse("S|BASS|Bass\nS|MID|Mids|LEVELS|0|soft|loud|SPIN|0.2|0.2\n")
        assertEquals(listOf(ReactSourceInfo(1, "MID", "Mids", "LEVELS", false, "soft", "loud", ReactMode.SPIN, 0.2f, 0.2f)), c.sources)
    }

    @Test
    fun aCellIsTheSourcesOwnSmartRoute() {
        val text =
            ReactSource.entries.joinToString("\n") {
                val mode = if (it == ReactSource.HARD_CUT) "JUMP" else "FOLLOW"
                "S|${it.name}|${it.label}|G|0|a|b|$mode|0.25|0.6"
            } + "\nL|0.35|0.7|1.2|-0.6\n"
        val c = ParamMatrixCatalog.parse(text)
        val cut = c.cell(ReactSource.HARD_CUT, LfoTarget.CAM_CUT, 1f)
        assertEquals(ReactMode.JUMP, cut.mode)
        assertEquals(0.6f, cut.releaseSeconds)
        assertEquals(ReactMode.DRIVE, c.cell(ReactSource.MID_TIME, LfoTarget.SPEED, 1f, drive = true).mode)
        assertEquals(listOf(0.35f, 0.7f, 1.2f, -0.6f), c.cellSteps)
    }

    @Test
    fun presetsCarryTheirRoutesAndDropUnknownOrdinals() {
        val c =
            ParamMatrixCatalog.parse(
                "P|PUNCHY|Punchy|Kick-led.\nR|${ReactSource.KICK_ENV.ordinal}|${LfoTarget.CAM_PUSH.ordinal}|0|0.55|0|0.01|0.25\n" +
                    "R|999|1|0|1|0|0|0\nP|CALM|Calm|Slow.\nR|${ReactSource.MID_TIME.ordinal}|${LfoTarget.CAM_FLY.ordinal}|5|1|3|0.3|0.6\n",
            )
        assertEquals(listOf("PUNCHY", "CALM"), c.presets.map { it.id })
        assertEquals(
            listOf(ReactionRoute(true, ReactSource.KICK_ENV, LfoTarget.CAM_PUSH, ReactMode.FOLLOW, 0.55f, ModCurve.LINEAR, 0.01f, 0.25f)),
            c.presets[0].routes,
        )
        assertEquals(ReactMode.DRIVE, c.presets[1].routes.single().mode)
        assertEquals(ModCurve.SMOOTH, c.presets[1].routes.single().curve)
    }

    @Test
    fun theMirrorsMatchWhatTheCatalogueFormatCanCarry() {
        // The enums' own rows, written in the native format and read back, round-trip: the
        // instrumented test compares the same rows with the real native catalogue.
        val text =
            ReactSource.entries.joinToString("\n") {
                "S|${it.name}|${it.label}|${it.group.name}|${if (it.isEvent) 1 else 0}|a|b|FOLLOW|0.1|0.3"
            } + "\n" + LfoTarget.entries.joinToString("\n") { "T|${it.name}|${it.label}|G|0|0|1|1|-" }
        val parsed = ParamMatrixCatalog.parse(text)
        assertEquals(ReactSource.entries.map { it.name }, parsed.sources.map { it.name })
        assertEquals(LfoTarget.entries.map { it.name }, parsed.targets.map { it.name })
    }
}
