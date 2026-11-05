package dev.geode.ui

import dev.geode.data.PerformanceTake
import dev.geode.data.SaveTarget
import dev.geode.data.TakeInfo
import dev.geode.data.TakeRepository
import dev.geode.data.TakeVideo
import dev.geode.editor.ProjectId
import dev.geode.editor.ProjectRender
import dev.geode.editor.StudioProject
import dev.geode.render.scene.SceneParams
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Test
import java.io.File

/**
 * The ask-once rule for where a stopped take's video goes (stories 35 and 36 of #227, ADR 0008):
 * with no answer yet the capture waits for the save sheet; the sheet's answer saves it there and
 * lists it on the take's project, and is kept for later takes only when "Use for every take" is
 * ticked; a folder that cannot be used keeps the sheet up rather than saving somewhere unwritable;
 * dismissing the sheet still keeps the video, in the Videos library, this once.
 */
@OptIn(ExperimentalCoroutinesApi::class)
class TakeSaveTargetTest {
    private class FakeTakes(
        var stored: SaveTarget? = null,
        var accepts: Boolean = true,
    ) : TakeRepository {
        val chosen = mutableListOf<Pair<SaveTarget, Boolean>>()

        override suspend fun list(): List<TakeInfo> = emptyList()

        override suspend fun projectOf(name: String): ProjectId = ProjectId(name)

        override suspend fun load(name: String): PerformanceTake.Timeline? = null

        override suspend fun save(
            name: String,
            json: String,
        ): String = name

        override suspend fun delete(name: String) = Unit

        override suspend fun rename(
            from: String,
            to: String,
        ): Boolean = true

        override suspend fun saveTarget(): SaveTarget? = stored

        override suspend fun chooseSaveTarget(
            target: SaveTarget,
            remember: Boolean,
        ): Boolean {
            chosen += target to remember
            if (!accepts) return false
            if (remember) stored = target
            return true
        }

        override suspend fun projectToRender(
            name: String,
            saveTo: SaveTarget,
        ): StudioProject = StudioProject(id = ProjectId(name), name = name)
    }

    private class FakeHost : TakeController.Host {
        /** Every capture copied out, with where it went. */
        val published = mutableListOf<Pair<TakeVideo, SaveTarget>>()

        /** The names listed on their project's renders, in order. */
        val rendered = mutableListOf<String>()

        override val vizState: StateFlow<VizUiState> = MutableStateFlow(VizUiState())
        override val activeMilkPath: String? = null
        override val trackUri: String? = null
        override val trackPositionMs: Long = 0L

        override fun selectScene(sceneId: String) = Unit

        override fun setSceneParams(params: SceneParams) = Unit

        override fun applyMilk(
            path: String,
            sceneId: String,
        ) = Unit

        override fun canRecord(): Boolean = true

        override fun onRecordLocked() = Unit

        override val soundFromInput: Boolean = false

        override fun beginCapture(fromInput: Boolean): Boolean = true

        override fun captureReachedCap(): Boolean = false

        override fun reportToCapture(event: dev.geode.data.TakeEvent): Boolean = false

        override fun endCapture(): TakeVideo? = null

        override fun publishCapture(
            video: TakeVideo,
            target: SaveTarget,
        ): String {
            published += video to target
            return "content://media/${published.size}"
        }

        override fun discardCapture(video: TakeVideo) = Unit

        override fun recordRender(
            id: ProjectId,
            render: ProjectRender,
        ) {
            rendered += render.label
        }
    }

    private fun video(name: String) = TakeVideo(File("$name.mp4"), 2_000L)

    private fun TestScope.controller(
        takes: FakeTakes,
        host: FakeHost,
    ) = TakeController(takes, this, this, host) { "Take $it" }

    @Test
    fun theFirstTakeWaitsForTheSheetInsteadOfRendering() =
        runTest {
            val host = FakeHost()
            val c = controller(FakeTakes(stored = null), host)
            c.onTakeSaved("Take 1", video("Take 1"))
            advanceUntilIdle()
            assertEquals("Take 1", c.state.value.pendingSave)
            assertEquals(0, host.rendered.size)
        }

    @Test
    fun aKeptAnswerRendersLaterTakesWithoutAsking() =
        runTest {
            val host = FakeHost()
            val c = controller(FakeTakes(stored = SaveTarget.VideosLibrary), host)
            c.onTakeSaved("Take 2", video("Take 2"))
            advanceUntilIdle()
            assertNull(c.state.value.pendingSave)
            assertEquals(listOf("Take 2"), host.rendered)
        }

    @Test
    fun answeringTheSheetRendersTheWaitingTakeAndKeepsTheAnswer() =
        runTest {
            val takes = FakeTakes()
            val host = FakeHost()
            val c = controller(takes, host)
            c.onTakeSaved("Take 1", video("Take 1"))
            advanceUntilIdle()
            c.chooseSaveTarget(SaveTarget.VideosLibrary, remember = true)
            advanceUntilIdle()
            assertNull(c.state.value.pendingSave)
            assertEquals(listOf("Take 1"), host.rendered)
            assertEquals(SaveTarget.VideosLibrary, takes.stored)
            // The next take goes straight to the kept answer.
            c.onTakeSaved("Take 2", video("Take 2"))
            advanceUntilIdle()
            assertNull(c.state.value.pendingSave)
            assertEquals(listOf("Take 1", "Take 2"), host.rendered)
        }

    @Test
    fun anAnswerNotKeptRendersThisTakeOnlyAndTheNextStopAsksAgain() =
        runTest {
            val takes = FakeTakes()
            val host = FakeHost()
            val c = controller(takes, host)
            c.onTakeSaved("Take 1", video("Take 1"))
            advanceUntilIdle()
            c.chooseSaveTarget(SaveTarget.VideosLibrary, remember = false)
            advanceUntilIdle()
            assertEquals(listOf(SaveTarget.VideosLibrary to false), takes.chosen)
            assertEquals(listOf("Take 1"), host.rendered)
            assertNull(takes.stored)
            c.onTakeSaved("Take 2", video("Take 2"))
            advanceUntilIdle()
            assertEquals("Take 2", c.state.value.pendingSave)
            assertEquals(1, host.rendered.size)
        }

    @Test
    fun aFolderThatCannotBeUsedKeepsTheSheetUpAndRendersNothing() =
        runTest {
            val takes = FakeTakes(accepts = false)
            val host = FakeHost()
            val c = controller(takes, host)
            c.onTakeSaved("Take 1", video("Take 1"))
            advanceUntilIdle()
            c.chooseSaveTarget(SaveTarget.Folder("content://tree/gone"), remember = true)
            // The note shows at once; idling past its timeout would clear it again.
            testScheduler.runCurrent()
            assertEquals("Take 1", c.state.value.pendingSave)
            assertNotNull(c.state.value.note)
            assertEquals(0, host.rendered.size)
            advanceUntilIdle()
        }

    @Test
    fun skippingTheSheetSavesToTheLibraryThisOnceAndTheNextStopAsksAgain() =
        runTest {
            val takes = FakeTakes()
            val host = FakeHost()
            val c = controller(takes, host)
            c.onTakeSaved("Take 1", video("Take 1"))
            advanceUntilIdle()
            c.skipPendingSave()
            advanceUntilIdle()
            assertNull(c.state.value.pendingSave)
            assertEquals(listOf(SaveTarget.VideosLibrary), host.published.map { it.second })
            assertEquals(listOf("Take 1"), host.rendered)
            assertNull(takes.stored)
            c.onTakeSaved("Take 2", video("Take 2"))
            advanceUntilIdle()
            assertEquals("Take 2", c.state.value.pendingSave)
        }

    @Test
    fun theSavedVideoIsTheCaptureItself() =
        runTest {
            val host = FakeHost()
            val c = controller(FakeTakes(stored = SaveTarget.Folder("content://tree/videos")), host)
            val captured = video("Take 3")
            c.onTakeSaved("Take 3", captured)
            advanceUntilIdle()
            assertEquals(listOf(captured to SaveTarget.Folder("content://tree/videos")), host.published)
        }
}
