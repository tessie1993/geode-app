package dev.geode.model

import dev.geode.model.OpenedAudio.Route
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** Every value here comes from another app's intent or provider, so the refusals are what is tested. */
class OpenedAudioTest {
    private val view = "android.intent.action.VIEW"
    private val own = "com.beatrealms3d.app"

    private fun route(
        action: String? = view,
        scheme: String? = "content",
        type: String? = "audio/mpeg",
        segment: String? = "song.mp3",
        fromHistory: Boolean = false,
    ) = OpenedAudio.route(action, scheme, type, segment, fromHistory)

    @Test
    fun `a content view with an audio type opens`() {
        assertEquals(Route.Open, route())
        assertEquals(Route.Open, route(type = "audio/flac"))
        assertEquals(Route.Open, route(type = "AUDIO/Ogg; codecs=opus"))
        assertEquals(Route.Open, route(type = "application/ogg"))
        assertEquals(Route.Open, route(type = "application/x-flac"))
    }

    @Test
    fun `an untyped content view opens unless it is a milk preset`() {
        assertEquals(Route.Open, route(type = null, segment = "1234"))
        assertEquals(Route.NotOurs, route(type = null, segment = "Aderack - Space.MILK"))
    }

    @Test
    fun `other actions, schemes and types are left to the other handlers`() {
        assertEquals(Route.NotOurs, route(action = "android.intent.action.SEND"))
        assertEquals(Route.NotOurs, route(action = null))
        assertEquals(Route.NotOurs, route(scheme = "file"))
        assertEquals(Route.NotOurs, route(scheme = "CONTENT"))
        assertEquals(Route.NotOurs, route(scheme = "geode"))
        assertEquals(Route.NotOurs, route(scheme = null))
        assertEquals(Route.NotOurs, route(type = "video/mp4"))
        assertEquals(Route.NotOurs, route(type = "audio/"))
        assertEquals(Route.NotOurs, route(type = "application/octet-stream", segment = "x.milk"))
        assertEquals(Route.NotOurs, route(type = "*/*"))
    }

    @Test
    fun `a view replayed from Recents is dropped`() {
        assertEquals(Route.Stale, route(fromHistory = true))
        assertEquals(Route.NotOurs, route(type = "video/mp4", fromHistory = true))
    }

    private fun refuses(
        scheme: String? = "content",
        authority: String? = "com.example.files.provider",
        provider: String? = "com.example.files",
        granted: Boolean = true,
    ) = OpenedAudio.refuses(scheme, authority, own, provider, granted)

    @Test
    fun `a granted uri of another app's provider plays`() {
        assertFalse(refuses())
        assertFalse(refuses(authority = "media", provider = "com.android.providers.media.module"))
    }

    @Test
    fun `the app's own providers are refused`() {
        assertTrue(refuses(authority = "$own.presets", provider = own))
        assertTrue(refuses(authority = "$own.presets", provider = "com.example.files"))
        assertTrue(refuses(authority = own, provider = null))
        assertTrue(refuses(authority = "lib.authority", provider = own))
    }

    @Test
    fun `an ungranted, unknown or malformed uri is refused`() {
        assertTrue(refuses(granted = false))
        assertTrue(refuses(provider = null))
        assertTrue(refuses(authority = null))
        assertTrue(refuses(authority = " "))
        assertTrue(refuses(scheme = "file"))
        assertTrue(refuses(scheme = "android.resource"))
    }

    @Test
    fun `a name loses its extension and keeps its words`() {
        assertEquals("Song", OpenedAudio.displayName("Song.mp3"))
        assertEquals("Mr. Blue Sky", OpenedAudio.displayName("Mr. Blue Sky"))
        assertEquals("Mr. Blue Sky", OpenedAudio.displayName("Mr. Blue Sky.flac"))
        assertEquals(".mp3", OpenedAudio.displayName(".mp3"))
    }

    @Test
    fun `control and bidi characters are dropped and whitespace folded`() {
        assertEquals("evil", OpenedAudio.displayName("‮evil"))
        assertEquals("a b", OpenedAudio.displayName("  a\n\t  b\u0000  "))
        assertEquals("ab", OpenedAudio.displayName("a​b"))
        assertEquals("ab", OpenedAudio.displayName("ab"))
        assertEquals("ab", OpenedAudio.displayName("a\uD800b"))
    }

    @Test
    fun `nothing visible is no name`() {
        assertNull(OpenedAudio.displayName(null))
        assertNull(OpenedAudio.displayName(""))
        assertNull(OpenedAudio.displayName(" ‮\u0007 "))
    }

    @Test
    fun `a long name is capped without splitting a surrogate pair`() {
        val emoji = "🎵"
        val name = OpenedAudio.displayName(emoji.repeat(500))
        requireNotNull(name)
        assertEquals(OpenedAudio.MAX_NAME_CODE_POINTS, name.codePointCount(0, name.length))
        assertTrue(name.endsWith(emoji))
        assertEquals(OpenedAudio.MAX_NAME_CODE_POINTS, OpenedAudio.displayName("x".repeat(10_000))?.length)
    }

    @Test
    fun `opened ids are fresh, marked, and carry no uri`() {
        val a = OpenedAudio.mediaId()
        val b = OpenedAudio.mediaId()
        assertTrue(OpenedAudio.isOpened(a))
        assertTrue(a != b)
        assertFalse(OpenedAudio.isOpened("content://media/external/audio/media/1"))
        assertFalse(OpenedAudio.isOpened(null))
    }
}
