package dev.geode.data

import androidx.test.ext.junit.runners.AndroidJUnit4
import dev.geode.model.StyleCatalogue
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Test
import org.junit.runner.RunWith

/** A saved look whose style this build no longer has is skipped when read, never applied and never a crash. */
@RunWith(AndroidJUnit4::class)
class PresetStoreStyleDeviceTest {
    @Test
    fun aLookOfAStyleThatIsGoneIsRefused() {
        val gone = PresetStore.toJson(Preset("Old look", "a_style_this_build_lacks", 0.5f, 0.5f))
        assertThrows(IllegalArgumentException::class.java) { PresetStore.fromJson(gone) }
    }

    @Test
    fun aLookOfAStyleThatExistsIsRead() {
        val style = StyleCatalogue.ids.first()
        val read = PresetStore.fromJson(PresetStore.toJson(Preset("Kept look", style, 0.5f, 0.5f)))
        assertEquals(style, read.sceneId)
    }
}
