package dev.geode.render

import org.junit.Assert.assertEquals
import org.junit.Test

class ReactSourceCatalogTest {
    @Test
    fun parsesLinesInWireOrder() {
        val c = ReactSourceCatalog.parse("BASS|Bass|LEVELS|0\nKICK|Kick hit|DRUMS|1\n")
        assertEquals(
            listOf(ReactSourceInfo(0, "BASS", "Bass", "LEVELS", false), ReactSourceInfo(1, "KICK", "Kick hit", "DRUMS", true)),
            c,
        )
    }

    @Test
    fun theMirrorMatchesWhatTheCatalogueFormatCanCarry() {
        // The enum's own rows, written in the native format and read back, round-trip: the
        // instrumented test compares the same rows with the real native catalogue.
        val text = ReactSource.entries.joinToString("\n") { "${it.name}|${it.label}|${it.group.name}|${if (it.isEvent) 1 else 0}" }
        val parsed = ReactSourceCatalog.parse(text)
        assertEquals(ReactSource.entries.map { it.name }, parsed.map { it.name })
        assertEquals(ReactSource.entries.map { it.isEvent }, parsed.map { it.isEvent })
    }
}
