package dev.geode.model

import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * Settings › Preferences › Library's stored choices: a missing or renamed value reads as the
 * behaviour Library had before the choice existed, never as some other tab or tap.
 */
class LibraryChoicesTest {
    @Test
    fun anUnknownNameIsTheOldBehaviour() {
        assertEquals(LibraryStartTab.TRACKS, LibraryStartTab.fromName(null))
        assertEquals(LibraryStartTab.TRACKS, LibraryStartTab.fromName("DUPLICATES"))
        assertEquals(TrackTap.PLAY_LIST, TrackTap.fromName(null))
        assertEquals(TrackTap.PLAY_LIST, TrackTap.fromName("SHUFFLE"))
    }

    @Test
    fun everyChoiceReadsBackByItsName() {
        LibraryStartTab.entries.forEach { assertEquals(it, LibraryStartTab.fromName(it.name)) }
        TrackTap.entries.forEach { assertEquals(it, TrackTap.fromName(it.name)) }
    }
}
