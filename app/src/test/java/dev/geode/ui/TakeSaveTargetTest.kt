package dev.geode.ui

import dev.geode.data.PerformanceTake
import dev.geode.data.SaveTarget
import dev.geode.data.TakeInfo
import dev.geode.data.TakeRepository
import dev.geode.data.VizUiState
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

/**
 * The ask-once rule for where a stopped take renders (stories 35 and 36 of #227): with no answer
 * yet the take waits for the save sheet instead of rendering; the sheet's answer renders it, and is
 * kept for later takes only when "Use for every take" is ticked; a folder that cannot be used keeps
 * the sheet up rather than rendering somewhere unwritable.
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
        val rendered = mutableListOf<StudioProject>()

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

        override fun lengthLimitNote(recordedMs: Long): String? = null

        override fun canRecord(): Boolean = true

        override fun onRecordLocked() = Unit

        override fun renderProject(
            project: StudioProject,
            onRendered: (ProjectRender) -> Unit,
        ) {
            rendered += project
        }

        override fun recordRender(
            id: ProjectId,
            render: ProjectRender,
        ) = Unit
    }

    private fun TestScope.controller(
        takes: FakeTakes,
        host: FakeHost,
    ) = TakeController(takes, this, this, host) { "Take $it" }

    @Test
    fun theFirstTakeWaitsForTheSheetInsteadOfRendering() =
        runTest {
            val host = FakeHost()
            val c = controller(FakeTakes(stored = null), host)
            c.onTakeSaved("Take 1")
            advanceUntilIdle()
            assertEquals("Take 1", c.state.value.pendingSave)
            assertEquals(0, host.rendered.size)
        }

    @Test
    fun aKeptAnswerRendersLaterTakesWithoutAsking() =
        runTest {
            val host = FakeHost()
            val c = controller(FakeTakes(stored = SaveTarget.VideosLibrary), host)
            c.onTakeSaved("Take 2")
            advanceUntilIdle()
            assertNull(c.state.value.pendingSave)
            assertEquals(listOf("Take 2"), host.rendered.map { it.name })
        }

    @Test
    fun answeringTheSheetRendersTheWaitingTakeAndKeepsTheAnswer() =
        runTest {
            val takes = FakeTakes()
            val host = FakeHost()
            val c = controller(takes, host)
            c.onTakeSaved("Take 1")
            advanceUntilIdle()
            c.chooseSaveTarget(SaveTarget.VideosLibrary, remember = true)
            advanceUntilIdle()
            assertNull(c.state.value.pendingSave)
            assertEquals(listOf("Take 1"), host.rendered.map { it.name })
            assertEquals(SaveTarget.VideosLibrary, takes.stored)
            // The next take goes straight to the kept answer.
            c.onTakeSaved("Take 2")
            advanceUntilIdle()
            assertNull(c.state.value.pendingSave)
            assertEquals(listOf("Take 1", "Take 2"), host.rendered.map { it.name })
        }

    @Test
    fun anAnswerNotKeptRendersThisTakeOnlyAndTheNextStopAsksAgain() =
        runTest {
            val takes = FakeTakes()
            val host = FakeHost()
            val c = controller(takes, host)
            c.onTakeSaved("Take 1")
            advanceUntilIdle()
            c.chooseSaveTarget(SaveTarget.VideosLibrary, remember = false)
            advanceUntilIdle()
            assertEquals(listOf(SaveTarget.VideosLibrary to false), takes.chosen)
            assertEquals(listOf("Take 1"), host.rendered.map { it.name })
            assertNull(takes.stored)
            c.onTakeSaved("Take 2")
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
            c.onTakeSaved("Take 1")
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
    fun skippingTheSheetLeavesTheTakeUnrendered() =
        runTest {
            val host = FakeHost()
            val c = controller(FakeTakes(), host)
            c.onTakeSaved("Take 1")
            advanceUntilIdle()
            c.skipPendingSave()
            advanceUntilIdle()
            assertNull(c.state.value.pendingSave)
            assertEquals(0, host.rendered.size)
        }
}
