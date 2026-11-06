package dev.geode.data

import org.junit.Assert.assertEquals
import org.junit.Test

/** An untagged file's "<unknown>" from MediaStore never reaches the library or the mini player. */
class DeviceTrackTagTest {
    @Test
    fun mediaStoresUnknownReadsAsTheFallback() {
        assertEquals("Unknown artist", DeviceTrackQuery.tagOr("<unknown>", "Unknown artist"))
    }

    @Test
    fun aMissingOrBlankTagReadsAsTheFallback() {
        assertEquals("Unknown artist", DeviceTrackQuery.tagOr(null, "Unknown artist"))
        assertEquals("Unknown artist", DeviceTrackQuery.tagOr("  ", "Unknown artist"))
    }

    @Test
    fun aRealTagIsKept() {
        assertEquals("Björk", DeviceTrackQuery.tagOr("Björk", "Unknown artist"))
    }
}
