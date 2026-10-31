package dev.geode.render

import org.junit.Assert.assertEquals
import org.junit.Test

class ParamMatrixCatalogTest {
    @Test
    fun parsesSourcesAndTargetsInWireOrder() {
        val c = ParamMatrixCatalog.parse("S|BASS|Bass|LEVELS|0\nS|KICK|Kick hit|DRUMS|1\nT|NONE|None|NONE|0\nT|BLOOM|Bloom|FX|1\n")
        assertEquals(
            listOf(ReactSourceInfo(0, "BASS", "Bass", "LEVELS", false), ReactSourceInfo(1, "KICK", "Kick hit", "DRUMS", true)),
            c.sources,
        )
        assertEquals(
            listOf(ReactTargetInfo(0, "NONE", "None", "NONE", false), ReactTargetInfo(1, "BLOOM", "Bloom", "FX", true)),
            c.targets,
        )
    }

    @Test
    fun aMalformedLineKeepsTheIdsOfTheRest() {
        val c = ParamMatrixCatalog.parse("S|BASS|Bass\nS|MID|Mids|LEVELS|0\n")
        assertEquals(listOf(ReactSourceInfo(1, "MID", "Mids", "LEVELS", false)), c.sources)
    }

    @Test
    fun theMirrorsMatchWhatTheCatalogueFormatCanCarry() {
        // The enums' own rows, written in the native format and read back, round-trip: the
        // instrumented test compares the same rows with the real native catalogue.
        val text =
            ReactSource.entries.joinToString("\n") { "S|${it.name}|${it.label}|${it.group.name}|${if (it.isEvent) 1 else 0}" } +
                "\n" + LfoTarget.entries.joinToString("\n") { "T|${it.name}|${it.label}|G|0" }
        val parsed = ParamMatrixCatalog.parse(text)
        assertEquals(ReactSource.entries.map { it.name }, parsed.sources.map { it.name })
        assertEquals(LfoTarget.entries.map { it.name }, parsed.targets.map { it.name })
    }
}
