package dev.geode.ui

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * PresetLink is the one place a hostile string reaches the app: a preset arrives
 * whole inside a link, from a chat, and is inflated on the main thread before the
 * first frame. The bounds are the interesting part, so they are what is tested.
 */
class PresetLinkTest {
    private val json = """{"name":"Kyanite","style":"raymarch","params":{"gain":0.75}}"""

    @Test
    fun `encode then decode returns the original document`() {
        assertEquals(json, PresetLink.decode(PresetLink.encode(json)))
    }

    @Test
    fun `a minted link is recognised as one`() {
        assertTrue(PresetLink.isPresetLink(PresetLink.encode(json)))
    }

    @Test
    fun `recognition ignores case and surrounding whitespace`() {
        assertTrue(PresetLink.isPresetLink("  GEODE://PRESET/abc  "))
        assertFalse(PresetLink.isPresetLink("https://example.com/preset/abc"))
    }

    @Test
    fun `a fragment or query after the payload is not part of it`() {
        val link = PresetLink.encode(json)
        assertEquals(json, PresetLink.decode("$link#fragment"))
        assertEquals(json, PresetLink.decode("$link?utm=share"))
    }

    @Test
    fun `a link past the length cap is refused before it is inflated`() {
        val overlong = "geode://preset/" + "A".repeat(PresetLink.MAX_LINK_LENGTH)
        assertTrue(overlong.length > PresetLink.MAX_LINK_LENGTH)
        assertNull(PresetLink.decode(overlong))
    }

    @Test
    fun `payload that is not valid gzip decodes to null rather than throwing`() {
        assertNull(PresetLink.decode("geode://preset/not-base64-gzip"))
    }

    @Test
    fun `something that is not a preset link at all decodes to null`() {
        assertNull(PresetLink.decode("https://example.com/"))
        assertNull(PresetLink.decode(""))
    }

    @Test
    fun `a link is found inside surrounding chat text`() {
        val link = PresetLink.encode(json)
        val found = PresetLink.findIn("look at this $link isn't it nice")
        assertEquals(link, found)
        assertEquals(json, PresetLink.decode(found!!))
    }

    @Test
    fun `text with no link in it yields null`() {
        assertNull(PresetLink.findIn("no link here"))
    }
}
