package dev.geode.widget

import android.content.Context
import androidx.test.core.app.ApplicationProvider
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNotSame
import org.junit.Assert.assertSame
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

/**
 * The widget's pictures are drawn again only when what they are drawn from changes: the 20 s
 * progress tick moves the position alone, and must not cost a read and a decode.
 */
@RunWith(RobolectricTestRunner::class)
class WidgetVisualTest {
    @get:Rule
    val tmp = TemporaryFolder()

    @Before
    fun noNativeLibrary() {
        WidgetVisual.lightsOf = { FloatArray(18) { 0.5f } }
    }

    @After
    fun restoreLights() {
        WidgetVisual.lightsOf = original
    }

    private val original = WidgetVisual.lightsOf

    private val context = ApplicationProvider.getApplicationContext<Context>()

    // No such thumbnail in the assets, so this style draws the glow.
    private val state = WidgetState(title = "Song", styleId = "no_such_style", artHue = 0.3f, artSpan = 0.1f, tick = 4)

    @Test
    fun aPositionChangeKeepsThePicture() {
        val first = WidgetVisual.render(context, state)

        val next = WidgetVisual.render(context, state.copy(positionMs = 20_000L, playing = true), first)

        assertSame(first, next)
    }

    @Test
    fun aTickMovesTheGlow() {
        val first = WidgetVisual.render(context, state)

        val next = WidgetVisual.render(context, state.copy(tick = 5), first)

        assertNotSame(first.bitmap, next.bitmap)
        assertEquals(WidgetVisual.Glow("no_such_style", 0.3f, 0.1f, 5), next.source)
    }

    @Test
    fun anotherStyleOrPaletteDrawsANewPicture() {
        val first = WidgetVisual.render(context, state)

        assertNotSame(first, WidgetVisual.render(context, state.copy(styleId = "another_missing_style"), first))
        assertNotSame(first, WidgetVisual.render(context, state.copy(artHue = 0.7f), first))
        assertNotSame(first, WidgetVisual.render(context, state.copy(styleId = null), first))
    }

    @Test
    fun aThumbnailIsKeptWhileTheStyleIsTheSame() {
        val thumbnail = WidgetVisual.Picture(WidgetVisual.render(context, state).bitmap, WidgetVisual.Thumbnail("no_such_style"))

        assertSame(thumbnail, WidgetVisual.render(context, state.copy(tick = 9, artHue = 0.9f), thumbnail))
        assertNotSame(thumbnail, WidgetVisual.render(context, state.copy(styleId = "another_missing_style"), thumbnail))
    }

    @Test
    fun theArtworkKeyChangesWhenTheFileIsOverwritten() {
        val file = tmp.newFile("widget_art.png")
        file.writeBytes(ByteArray(10))
        val before = ArtKey.of(file.path)
        assertEquals(before, ArtKey.of(file.path))

        file.writeBytes(ByteArray(20))

        assertNotEquals(before, ArtKey.of(file.path))
    }
}
