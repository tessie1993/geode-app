package dev.geode.ui

import dev.geode.data.EditorProjectStore
import dev.geode.data.ExportDestination
import dev.geode.editor.Clip
import dev.geode.editor.ClipContent
import dev.geode.editor.ClipId
import dev.geode.editor.IdSource
import dev.geode.editor.Lane
import dev.geode.editor.LaneId
import dev.geode.editor.LaneKind
import dev.geode.editor.ProjectId
import dev.geode.editor.ProjectRender
import dev.geode.editor.StudioProject
import dev.geode.editor.TrackRef
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import java.util.UUID

/**
 * Studio's project folders as My videos sees them, newest first. [loaded] turns true once the
 * folders have been read the first time, so an empty list can mean "nothing yet" rather than "not
 * read yet".
 */
data class EditorUiState(
    val loaded: Boolean = false,
    val projects: List<StudioProject> = emptyList(),
)

/**
 * Keeps the list of Studio's project folders (`files/projects/<id>/`) and the renders each one
 * lists. Studio has no editor: the owner dropped video editing from the launch (#227), so nothing
 * here opens, edits or undoes a project. A project is only how a recording and a kept render are
 * stored (ADR 0005): a saved recording becomes one in [keepRecording], and a one-tap render the
 * user adds to My videos becomes one in [createWithLook].
 *
 * The render path records each finished file with [recordRender], so a render that finishes after
 * the user moved on still lands in its project. The store is touched only on [storeScope], one
 * write at a time, so a list read always follows the write before it.
 */
internal class EditorController(
    private val store: EditorProjectStore,
    private val storeScope: CoroutineScope,
) {
    private val _state = MutableStateFlow(EditorUiState())
    val state: StateFlow<EditorUiState> = _state

    private val ids = IdSource { UUID.randomUUID().toString() }

    /**
     * Reads the project list once at start, first deleting the projects that list no video: only
     * the old replayed takes were saved without one, and the owner chose to delete those.
     */
    fun start() {
        storeScope.launch {
            store.list().filter { it.renders.isEmpty() }.forEach { store.delete(it.id) }
            refreshLists()
        }
    }

    /** A saved recording in My videos: a project over [track] (none for the mic or another app) listing [render]. */
    fun keepRecording(
        track: TrackRef?,
        render: ProjectRender,
    ) {
        storeScope.launch {
            val project =
                StudioProject.create(
                    id = store.newId(),
                    name = render.label.ifBlank { nextName(store.list()) },
                    destination = ExportDestination.CUSTOM,
                    defaults = store.defaultsFor(ExportDestination.CUSTOM),
                    nowMs = store.clock(),
                    ids = ids,
                    track = track,
                )
            store.save(project.withRender(render))
            refreshLists()
        }
    }

    /**
     * A one-tap render the user added to My videos: [track] on an audio lane and the look on stage
     * ([sceneId], plus [milkPath] for a MilkDrop preset) as one visual clip over the whole song,
     * listing the finished [render]. Made after the file exists, so a failed render leaves no folder.
     */
    fun createWithLook(
        track: TrackRef,
        sceneId: String,
        milkPath: String?,
        render: ProjectRender,
    ) {
        storeScope.launch {
            val base =
                StudioProject.create(
                    id = store.newId(),
                    name = track.title.ifBlank { nextName(store.list()) },
                    destination = ExportDestination.CUSTOM,
                    defaults = store.defaultsFor(ExportDestination.CUSTOM),
                    nowMs = store.clock(),
                    ids = ids,
                    track = track,
                )
            val lengthMs = maxOf(track.durationMs, render.durationMs)
            val look =
                Clip(
                    id = ClipId(ids.next()),
                    content = ClipContent.Scene(sceneId = sceneId, milkPath = milkPath),
                    startMs = 0L,
                    durationMs = lengthMs,
                )
            val visual = Lane(LaneId(ids.next()), LaneKind.Visual, clips = listOfNotNull(look.takeIf { lengthMs > 0L }))
            val song = base.edit.timeline
            val timeline = song.copy(lanes = listOf(visual) + song.lanes, durationMs = maxOf(song.durationMs, lengthMs))
            store.save(base.copy(edit = base.edit.copy(timeline = timeline)).withRender(render))
            refreshLists()
        }
    }

    /** Adds a finished video to project [id]'s list. */
    fun recordRender(
        id: ProjectId,
        render: ProjectRender,
    ) = changeProject(id) { it.withRender(render) }

    /** Takes a video off project [id]'s list (My videos' delete); the file itself is the caller's. */
    fun removeRender(
        id: ProjectId,
        uri: String,
    ) = changeProject(id) { it.withoutRender(uri) }

    private fun changeProject(
        id: ProjectId,
        change: (StudioProject) -> StudioProject,
    ) {
        storeScope.launch {
            store.load(id)?.let { store.save(store.touch(change(it))) }
            refreshLists()
        }
    }

    private suspend fun refreshLists() {
        val listed = store.list()
        _state.update { it.copy(loaded = true, projects = listed) }
    }

    private fun nextName(existing: List<StudioProject>): String {
        val taken = existing.map { it.name }.toSet()
        var n = existing.size + 1
        while ("$DEFAULT_NAME $n" in taken) n++
        return "$DEFAULT_NAME $n"
    }

    companion object {
        /** Prefix of the names new projects get ("Project 3") when the song has no title. */
        const val DEFAULT_NAME = "Project"
    }
}
