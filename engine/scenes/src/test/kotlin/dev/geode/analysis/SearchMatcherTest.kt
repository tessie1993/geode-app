package dev.geode.analysis

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class SearchMatcherTest {
    @Test
    fun foldStripsAccentsAndCase() {
        assertEquals("beyonce", SearchMatcher.fold("Beyoncé"))
        assertEquals("sigur ros", SearchMatcher.fold("Sigur Rós"))
        assertEquals("motorhead", SearchMatcher.fold("MOTÖRHEAD"))
        assertEquals("cafe", SearchMatcher.fold("Café"))
    }

    @Test
    fun foldSpellsOutLettersThatDoNotDecompose() {
        assertEquals("strasse", SearchMatcher.fold("Straße"))
        assertEquals("roisin", SearchMatcher.fold("Røisin"))
        assertEquals("lodz", SearchMatcher.fold("Łódź"))
        assertEquals("aether", SearchMatcher.fold("Æther"))
    }

    @Test
    fun foldIgnoresThePhoneLocale() {
        // "I" must fold to a dotted i whatever the default locale, or "INXS" stops matching "inxs".
        assertEquals("inxs", SearchMatcher.fold("INXS"))
        assertEquals("istanbul", SearchMatcher.fold("İstanbul"))
    }

    @Test
    fun plainQueryFindsAccentedTitle() {
        assertTrue(SearchMatcher.matches(SearchMatcher.terms("beyonce"), listOf("Beyoncé")))
        assertTrue(SearchMatcher.matches(SearchMatcher.terms("BEYONCÉ"), listOf("beyonce")))
        assertFalse(SearchMatcher.matches(SearchMatcher.terms("adele"), listOf("Beyoncé")))
    }

    @Test
    fun everyTermMustMatch() {
        val fields = listOf("Halo", "Beyoncé", "I Am... Sasha Fierce")
        assertTrue(SearchMatcher.matches(SearchMatcher.terms("halo beyonce"), fields))
        assertFalse(SearchMatcher.matches(SearchMatcher.terms("halo adele"), fields))
        assertFalse(SearchMatcher.matches(SearchMatcher.terms("   "), fields))
    }

    @Test
    fun tierIsTheFieldTheMatchNeeded() {
        val fields = listOf("Halo", "Beyoncé", "Sasha Fierce", "/music/pop").map(SearchMatcher::fold)
        assertEquals(SearchMatcher.TIER_TITLE, SearchMatcher.tier(SearchMatcher.terms("halo"), fields))
        assertEquals(SearchMatcher.TIER_ARTIST, SearchMatcher.tier(SearchMatcher.terms("beyonce"), fields))
        assertEquals(SearchMatcher.TIER_ALBUM, SearchMatcher.tier(SearchMatcher.terms("fierce"), fields))
        assertEquals(3, SearchMatcher.tier(SearchMatcher.terms("pop"), fields))
        // Split across title and artist: ranks as the artist, the furthest field it needed.
        assertEquals(SearchMatcher.TIER_ARTIST, SearchMatcher.tier(SearchMatcher.terms("halo beyonce"), fields))
        assertEquals(SearchMatcher.NO_MATCH, SearchMatcher.tier(SearchMatcher.terms("adele"), fields))
    }

    @Test
    fun filterTracksRanksTitleAboveArtistAboveAlbum() {
        data class Row(
            val uri: String,
            val fields: List<String>,
        )
        val rows =
            listOf(
                Row("album", listOf("Intro", "Someone", "Love Songs")),
                Row("artist", listOf("Intro", "Love Inc.", "Hits")),
                Row("title", listOf("Love Story", "Someone", "Hits")),
            )
        val result =
            SearchMatcher.filterTracks(
                terms = SearchMatcher.terms("love"),
                items = rows,
                uriOf = { it.uri },
                fieldsOf = { it.fields },
            )
        assertEquals(listOf("title", "artist", "album"), result.map { it.uri })
    }
}
