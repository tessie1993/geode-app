package dev.geode.ui

import dev.geode.model.TutorialChapter
import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * The tour's page arithmetic reads card counts from [TutorialChapter]; the cards themselves live
 * here in `ui`. These hold the two together, and keep each drawing to one card so no two cards
 * look alike.
 */
class TutorialCardsTest {
    @Test
    fun `every chapter has exactly as many cards as the model counts`() {
        TutorialChapter.entries.forEach { assertEquals(it.name, it.cards, it.cardContent.size) }
    }

    @Test
    fun `every drawing is used by exactly one card`() {
        val arts = TutorialChapter.entries.flatMap { it.cardContent }.map { it.art }
        assertEquals(TutorialArt.entries.toList(), arts.sortedBy { it.ordinal })
    }
}
