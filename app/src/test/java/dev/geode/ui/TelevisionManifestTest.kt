package dev.geode.ui

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.w3c.dom.Element
import java.io.File
import javax.xml.parsers.DocumentBuilderFactory

/**
 * What Play needs to offer the app on Android TV and Google TV (TV app quality, Tier 3: TV-ML,
 * TV-MT, TV-LB): read from the main manifest, which is the same for every build type. A required
 * hardware feature a TV lacks, declared or implied by a permission, hides the app from every TV.
 */
class TelevisionManifestTest {
    private val android = "http://schemas.android.com/apk/res/android"
    private val manifest =
        DocumentBuilderFactory
            .newInstance()
            .apply { isNamespaceAware = true }
            .newDocumentBuilder()
            .parse(File("src/main/AndroidManifest.xml"))
            .documentElement

    private fun elements(tag: String): List<Element> =
        manifest.getElementsByTagName(tag).let { list -> (0 until list.length).map { list.item(it) as Element } }

    private fun Element.attr(name: String): String = getAttributeNS(android, name)

    /** Each uses-feature by name, as its android:required (absent reads as Android's default, true). */
    private val features: Map<String, String> =
        elements("uses-feature")
            .filter { it.attr("name").isNotEmpty() }
            .associate { it.attr("name") to it.attr("required").ifEmpty { "true" } }

    @Test
    fun noHardwareFeatureIsRequired() {
        assertEquals(emptyMap<String, String>(), features.filterValues { it != "false" })
    }

    @Test
    fun aTvNeedsNoTouchscreenAndLeanbackIsOptional() {
        listOf("android.hardware.touchscreen", "android.hardware.faketouch", "android.software.leanback").forEach {
            assertEquals(it, "false", features[it])
        }
    }

    /** Permissions that imply a hardware feature ("Handle TV hardware"): each must say it is optional. */
    @Test
    fun everyImpliedFeatureIsDeclaredOptional() {
        val implied =
            mapOf(
                "android.permission.RECORD_AUDIO" to listOf("android.hardware.microphone"),
                "android.permission.CAMERA" to listOf("android.hardware.camera", "android.hardware.camera.autofocus"),
                "android.permission.ACCESS_WIFI_STATE" to listOf("android.hardware.wifi"),
                "android.permission.CHANGE_WIFI_STATE" to listOf("android.hardware.wifi"),
                "android.permission.BLUETOOTH" to listOf("android.hardware.bluetooth"),
                "android.permission.BLUETOOTH_CONNECT" to listOf("android.hardware.bluetooth"),
                "android.permission.CALL_PHONE" to listOf("android.hardware.telephony"),
                "android.permission.READ_PHONE_STATE" to listOf("android.hardware.telephony"),
            )
        val requested = elements("uses-permission").map { it.attr("name") }
        requested.flatMap { implied[it].orEmpty() }.forEach { feature ->
            assertEquals("$feature is implied by a permission", "false", features[feature])
        }
    }

    @Test
    fun theLauncherActivityIsOnTheTvLauncherWithABanner() {
        val launchers =
            elements("intent-filter").filter { filter ->
                val categories =
                    filter.getElementsByTagName("category").let { l ->
                        (0 until l.length).map { (l.item(it) as Element).attr("name") }
                    }
                "android.intent.category.LEANBACK_LAUNCHER" in categories
            }
        assertEquals(1, launchers.size)
        assertEquals(".ui.MainActivity", (launchers.single().parentNode as Element).attr("name"))
        val banner = elements("application").single().attr("banner")
        assertEquals("@drawable/tv_banner", banner)
        assertTrue(File("src/main/res/drawable/tv_banner.xml").isFile)
    }
}
