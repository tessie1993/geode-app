package dev.geode.render

import androidx.test.ext.junit.runners.AndroidJUnit4
import dev.geode.engine.bridge.GeodeNative
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith

/**
 * C++ owns the parameter matrix (core/viz/ParamMatrix); the Kotlin [ReactSource] and [LfoTarget]
 * mirrors must match it entry for entry, since their ordinals are what crosses the wire.
 */
@RunWith(AndroidJUnit4::class)
class ParamMatrixCatalogDeviceTest {
    private val native = ParamMatrixCatalog.parse(GeodeNative.paramMatrixCatalog())

    @Test
    fun theSourceMirrorMatchesTheNativeCatalogue() {
        val mirror = ReactSource.entries.map { listOf(it.name, it.label, it.group.name, it.isEvent.toString()) }
        assertEquals(mirror, native.sources.map { listOf(it.name, it.label, it.group, it.isEvent.toString()) })
        native.sources.forEach { assert(it.low.isNotBlank() && it.high.isNotBlank()) { "${it.name} names both ends" } }
    }

    @Test
    fun theTargetMirrorMatchesTheNativeCatalogue() {
        assertEquals(LfoTarget.entries.map { it.name }, native.targets.map { it.name })
        assertEquals(LfoTarget.entries.map { it.label }, native.targets.map { it.label })
    }
}
