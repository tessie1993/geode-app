package dev.geode.render

import androidx.test.ext.junit.runners.AndroidJUnit4
import dev.geode.engine.bridge.GeodeNative
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith

/**
 * C++ owns the reaction source catalogue (core/viz/Reactions.cpp); the Kotlin [ReactSource] mirror
 * must match it entry for entry (name, label, group, event) until the Customize feature reads the
 * native catalogue alone.
 */
@RunWith(AndroidJUnit4::class)
class ReactSourceCatalogDeviceTest {
    @Test
    fun theKotlinMirrorMatchesTheNativeCatalogue() {
        val native = ReactSourceCatalog.parse(GeodeNative.reactSourceCatalog())
        val mirror =
            ReactSource.entries.map { ReactSourceInfo(it.ordinal, it.name, it.label, it.group.name, it.isEvent) }
        assertEquals(mirror, native)
    }
}
