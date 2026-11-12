package dev.geode.render

import androidx.test.ext.junit.runners.AndroidJUnit4
import dev.geode.engine.bridge.GeodeNative
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith

/**
 * C++ owns the reaction source catalogue (core/viz/Reactions.cpp); the Kotlin [ReactSource] mirror
 * must match it line for line ("NAME|Label|GROUP|0/1", in wire order) until Customize reads the
 * native catalogue alone.
 */
@RunWith(AndroidJUnit4::class)
class ReactSourceCatalogDeviceTest {
    @Test
    fun theKotlinMirrorMatchesTheNativeCatalogue() {
        val native = GeodeNative.reactSourceCatalog().lines().filter { it.isNotBlank() }
        val mirror =
            ReactSource.entries.map { "${it.name}|${it.label}|${it.group.name}|${if (it.isEvent) 1 else 0}" }
        assertEquals(mirror, native)
    }
}
