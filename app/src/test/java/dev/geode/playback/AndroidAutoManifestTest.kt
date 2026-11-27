package dev.geode.playback

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.w3c.dom.Element
import java.io.File
import javax.xml.parsers.DocumentBuilderFactory

/** Android Auto lists a media app only when the application declares the car metadata, as a media app and nothing else. */
class AndroidAutoManifestTest {
    private val android = "http://schemas.android.com/apk/res/android"

    private fun parse(path: String): Element =
        DocumentBuilderFactory
            .newInstance()
            .apply { isNamespaceAware = true }
            .newDocumentBuilder()
            .parse(File(path))
            .documentElement

    private fun Element.children(tag: String): List<Element> =
        getElementsByTagName(tag).let { list -> (0 until list.length).map { list.item(it) as Element } }

    @Test
    fun theApplicationDeclaresItselfACarApp() {
        val application = parse("src/main/AndroidManifest.xml").children("application").single()
        val car =
            application.children("meta-data").filter {
                it.parentNode == application && it.getAttributeNS(android, "name") == "com.google.android.gms.car.application"
            }
        assertEquals(listOf("@xml/automotive_app_desc"), car.map { it.getAttributeNS(android, "resource") })
    }

    @Test
    fun theCarDescriptionUsesMediaOnly() {
        val file = File("src/main/res/xml/automotive_app_desc.xml")
        assertTrue(file.isFile)
        val description = parse(file.path)
        assertEquals("automotiveApp", description.tagName)
        assertEquals(listOf("media"), description.children("uses").map { it.getAttribute("name") })
    }

    @Test
    fun thePlaybackServiceIsAMediaBrowserService() {
        val service =
            parse("src/main/AndroidManifest.xml")
                .children("service")
                .single { it.getAttributeNS(android, "name") == ".playback.PlaybackService" }
        assertEquals("true", service.getAttributeNS(android, "exported"))
        assertTrue(service.children("action").any { it.getAttributeNS(android, "name") == "android.media.browse.MediaBrowserService" })
    }
}
