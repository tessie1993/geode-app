package dev.geode.model

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

/**
 * The packing step of a share link (gzip and base64). The link's framing, caps and finding are native's
 * (`core/tests/look_files_test.cpp`).
 */
class PresetLinkTest {
    private val json = """{"name":"Kyanite","style":"raymarch","params":{"gain":0.75}}"""

    @Test
    fun `pack then unpack returns the original document`() {
        assertEquals(json, LinkPacking.unpack(LinkPacking.pack(json), 1024))
    }

    @Test
    fun `payload that is not valid gzip unpacks to null rather than throwing`() {
        assertNull(LinkPacking.unpack("not-base64-gzip", 1024))
    }

    @Test
    fun `a payload that unpacks past the cap is refused`() {
        assertNull(LinkPacking.unpack(LinkPacking.pack("x".repeat(2048)), 1024))
    }
}
