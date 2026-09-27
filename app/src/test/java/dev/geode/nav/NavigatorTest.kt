package dev.geode.nav

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class NavigatorTest {
    @Test
    fun `restore keeps one correct root and only same section pages`() {
        val state =
            NavSaver.decode(
                """section=LIBRARY
                |stack.PLAYER=player/queue,player/lyrics,settings/about
                |stack.LIBRARY=library/ALBUMS,library/album/A%2FB,visuals/presets
                |stack.VISUALS=visuals/customize,visuals/presets
                |
                """.trimMargin(),
            )

        assertEquals(
            listOf(Destination.Player.NowPlaying, Destination.Player.Queue, Destination.Player.Lyrics),
            state.stacks.getValue(Section.PLAYER),
        )
        assertEquals(
            listOf(Destination.Library.Browse(LibraryView.ALBUMS), Destination.Library.Album("A/B")),
            state.stacks.getValue(Section.LIBRARY),
        )
        assertEquals(
            listOf(Destination.Visuals.Hub, Destination.Visuals.Customize, Destination.Visuals.Presets),
            state.stacks.getValue(Section.VISUALS),
        )
    }

    @Test
    fun `section switch restores its stack and selecting current section returns to root`() {
        val nav = Navigator()
        nav.go(Destination.Library.Album("Blue"))
        nav.show(Section.SETTINGS)
        nav.go(Destination.Settings.Audio)
        nav.show(Section.LIBRARY)
        assertEquals(Destination.Library.Album("Blue"), nav.state.value.current)

        nav.show(Section.LIBRARY)
        assertEquals(listOf(Destination.Library.Browse()), nav.state.value.stack)
        assertEquals(
            Destination.Settings.Audio,
            nav.state.value.stacks
                .getValue(Section.SETTINGS)
                .last(),
        )
    }

    @Test
    fun `root destination tap replaces section stack even when section is already selected`() {
        val nav = Navigator()
        nav.go(Destination.Visuals.Customize)
        nav.go(Destination.Visuals.Presets)
        nav.go(Destination.Visuals.Hub)

        assertEquals(listOf(Destination.Visuals.Hub), nav.state.value.stack)
    }

    @Test
    fun `foreign section replace replaces its saved top and preserves root`() {
        val nav = Navigator()
        nav.go(Destination.Library.Album("Blue"))
        nav.replace(Destination.Settings.Audio)

        assertEquals(Section.SETTINGS, nav.state.value.section)
        assertEquals(listOf(Destination.Settings.Root, Destination.Settings.Audio), nav.state.value.stack)
        assertEquals(
            listOf(Destination.Library.Browse(), Destination.Library.Album("Blue")),
            nav.state.value.stacks
                .getValue(Section.LIBRARY),
        )

        nav.replace(Destination.Library.Folder("/music"))
        assertEquals(listOf(Destination.Library.Browse(), Destination.Library.Folder("/music")), nav.state.value.stack)
    }

    @Test
    fun `back closes gate then overlay then sheet page then returns to player`() {
        val nav = Navigator()
        nav.go(Destination.Library.Browse())
        nav.go(Destination.Library.Album("Blue"))
        nav.go(Destination.Library.Album("Blue"))
        nav.open(Overlay.Search)
        nav.raiseGate(Gate.TUTORIAL)
        assertTrue(nav.back())
        assertNull(nav.state.value.gate)
        assertTrue(nav.back())
        assertNull(nav.state.value.overlay)
        assertTrue(nav.back())
        assertEquals(Destination.Library.Browse(), nav.state.value.current)
        assertTrue(nav.back())
        assertEquals(Section.PLAYER, nav.state.value.section)
        assertFalse(nav.back())
    }

    @Test
    fun `non-dismissible gate consumes back without dismissing overlays beneath it`() {
        val nav = Navigator()
        nav.open(Overlay.Search)
        nav.raiseGate(Gate.BOOT)
        assertFalse(nav.state.value.canGoBack)
        assertFalse(nav.back())
        assertEquals(Gate.BOOT, nav.state.value.gate)
        assertEquals(Overlay.Search, nav.state.value.overlay)
    }

    @Test
    fun `predictive back cancellation clears preview without changing navigation`() {
        val nav = Navigator()
        nav.go(Destination.Library.Album("Blue"))
        val before = nav.state.value
        nav.backStarted(BackGesture.Edge.LEFT)
        nav.backProgressed(.6f)
        assertEquals(
            Destination.Library.Browse(),
            nav.backGesture.value
                ?.target
                ?.current,
        )
        nav.backCancelled()

        assertNull(nav.backGesture.value)
        assertEquals(before, nav.state.value)
    }

    @Test
    fun `cold start deep link navigates and remains pending until matching handler consumes it`() {
        val link = DeepLink.Template("geode://template/one")
        val nav = Navigator(initialLink = link)
        assertEquals(link, nav.pendingLink.value)
        assertEquals(Section.PLAYER, nav.state.value.section)
        nav.handle(link)
        assertEquals(Section.STUDIO, nav.state.value.section)
        assertEquals(Destination.Studio.Templates, nav.state.value.current)

        nav.consumeLink(DeepLink.Preset("geode://preset/older"))
        assertEquals(link, nav.pendingLink.value)
        nav.consumeLink(link)
        assertNull(nav.pendingLink.value)
    }
}
