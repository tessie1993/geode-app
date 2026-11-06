package dev.geode.export

import dev.geode.editor.Marker
import dev.geode.editor.MarkerId
import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * The fallback chapter title is supplied by the caller (a localised string), never built here. The
 * spans themselves are native (core/output/Chapters) and pinned by core/tests/output_test.cpp,
 * including the 0:00 lead-in.
 */
class ChapterMarkersTest {
    private val markers =
        listOf(
            Marker(MarkerId("a"), atMs = 0L, name = "Intro"),
            Marker(MarkerId("b"), atMs = 10_000L, name = "  "),
            Marker(MarkerId("c"), atMs = 20_000L),
        )

    @Test
    fun `blank marker names take the callers title for their 1-based position`() {
        val titles = markers.indices.map { ChapterMarkers.markerTitle(markers, it, "Lead-in") { n -> "Kapitel $n" } }

        assertEquals(listOf("Intro", "Kapitel 2", "Kapitel 3"), titles)
    }

    @Test
    fun `the lead-in chapter takes the callers lead-in title`() {
        assertEquals("Vorspann", ChapterMarkers.markerTitle(markers, ChapterMarkers.LEAD_IN, "Vorspann") { "Kapitel $it" })
    }
}
