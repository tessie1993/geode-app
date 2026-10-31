package dev.geode.export

import dev.geode.editor.Marker
import dev.geode.editor.MarkerId
import dev.geode.editor.MarkerSet
import org.junit.Assert.assertEquals
import org.junit.Test

/** The fallback chapter title is supplied by the caller (a localised string), never built here. */
class ChapterMarkersTest {
    @Test
    fun `blank marker names take the callers title for their 1-based position`() {
        val markers =
            MarkerSet()
                .add(Marker(MarkerId("a"), atMs = 0L, name = "Intro"))
                .add(Marker(MarkerId("b"), atMs = 10_000L, name = "  "))
                .add(Marker(MarkerId("c"), atMs = 20_000L))

        val chapters = ChapterMarkers.of(markers, totalDurationMs = 30_000L) { "Kapitel $it" }

        assertEquals(listOf("Intro", "Kapitel 2", "Kapitel 3"), chapters.chapters.map { it.title })
    }
}
