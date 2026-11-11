package dev.geode.di

import dev.geode.engine.bridge.GeodeNative
import dev.geode.ui.ArtworkCache
import dev.geode.ui.trimStyleThumbs

/**
 * What the app gives back when Android asks for memory (#854), called by `GeodeApp.onTrimMemory`.
 *
 * WHY here: the caches live in `ui`, which the layer gate keeps `GeodeApp` from naming; `di` may.
 * The renderers' spare scenes are native, so the level goes across as is and `core/viz/SceneResidency`
 * decides (it acts on UI_HIDDEN and BACKGROUND, the two levels the platform still delivers; the rest
 * are gone since Android 14 and deprecated in 15). The decoded artwork and style thumbnails are
 * plain caches that decode again on demand, so they are emptied on the same two levels.
 */
object MemoryTrim {
    // ComponentCallbacks2.TRIM_MEMORY_UI_HIDDEN and TRIM_MEMORY_BACKGROUND (the latter deprecated from
    // Android 15; its value is the contract native matches).
    private const val UI_HIDDEN = 20
    private const val BACKGROUND = 40

    fun onTrim(level: Int) {
        if (level != UI_HIDDEN && level != BACKGROUND) return
        GeodeNative.memoryTrim(level)
        ArtworkCache.clear()
        trimStyleThumbs()
    }
}
