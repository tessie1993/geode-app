package dev.geode.ui

import dev.geode.data.EditorProjectStore
import dev.geode.data.ExportDestination
import dev.geode.editor.ProjectId
import dev.geode.editor.ProjectRender
import dev.geode.editor.StudioProject
import dev.geode.editor.TrackRef
import dev.geode.engine.bridge.GeodeNative
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

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
            newProject(render.label.ifBlank { nextName(store.list()) }, track, null, null, render)?.let(store::save)
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
            newProject(track.title.ifBlank { nextName(store.list()) }, track, sceneId, milkPath, render)?.let(store::save)
            refreshLists()
        }
    }

    /** A new project listing [render], its edit made by native; null only when native could not make one. */
    private fun newProject(
        name: String,
        track: TrackRef?,
        sceneId: String?,
        milkPath: String?,
        render: ProjectRender,
    ): StudioProject? {
        val kind = GeodeNative.studioKindRead(null) ?: return null
        val edit =
            GeodeNative.studioEditNew(track?.uri, track?.title.orEmpty(), track?.durationMs ?: 0L, sceneId, milkPath, render.durationMs)
                ?: return null
        val now = store.clock()
        return StudioProject(
            id = store.newId(),
            name = name,
            kind = kind,
            edit = edit,
            destination = ExportDestination.CUSTOM,
            export = store.defaultsFor(ExportDestination.CUSTOM),
            createdAtMs = now,
            updatedAtMs = now,
        ).withRender(render)
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
