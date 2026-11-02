package dev.geode.ui

import dev.geode.data.EditorProjectStore
import dev.geode.data.ExportDefaults
import dev.geode.data.ExportDestination
import dev.geode.editor.Clip
import dev.geode.editor.ClipContent
import dev.geode.editor.ClipId
import dev.geode.editor.EditResult
import dev.geode.editor.EditorHistory
import dev.geode.editor.EditorProject
import dev.geode.editor.IdSource
import dev.geode.editor.KeyframeId
import dev.geode.editor.Lane
import dev.geode.editor.LaneId
import dev.geode.editor.LaneKind
import dev.geode.editor.MarkerId
import dev.geode.editor.ProjectId
import dev.geode.editor.ProjectKind
import dev.geode.editor.ProjectRender
import dev.geode.editor.ProjectTemplate
import dev.geode.editor.StudioProject
import dev.geode.editor.TrackRef
import dev.geode.editor.toTemplate
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.util.UUID

/**
 * Studio's projects as the screens see them.
 *
 * [project] is the open project's edit, the one undo works on; [open] is the whole open project
 * (kind, export settings, renders) with that edit in it. [projects] and [templates] are the lists
 * Studio shows, newest project first. [id] is null until a project exists: the first edit creates
 * one ([EditorController.edit]), so opening Studio never leaves an empty folder behind.
 */
data class EditorUiState(
    val id: ProjectId? = null,
    val name: String = "",
    val history: EditorHistory = EditorHistory(EditorProject()),
    val loaded: Boolean = false,
    val playheadMs: Long = 0L,
    val projects: List<StudioProject> = emptyList(),
    val templates: List<ProjectTemplate> = emptyList(),
    internal val folder: StudioProject? = null,
) {
    val project: EditorProject get() = history.present

    val open: StudioProject? get() = folder?.copy(edit = history.present)
}

/**
 * Owns the open project and the project list: every edit goes through [edit] so it lands on the
 * undo stack and on disk. Edits run on the main thread; the store is touched only on [storeScope],
 * one write at a time, so a save always lands after the edit it saves.
 *
 * Making projects:
 * - [create]: a new project for a destination, its export settings copied from Settings › Export;
 * - [createFromTemplate]: a template fitted to a track;
 * - [createBatch]: one project per track from a template, for a batch render (none is opened);
 * - takes become projects on their own when a recording stops (`TakeController`, `data/TakeStore`).
 *
 * The render path records each finished file with [recordRender], which works for any project,
 * open or not, so a render that finishes after the user moved on still lands in its project.
 */
internal class EditorController(
    private val store: EditorProjectStore,
    private val scope: CoroutineScope,
    private val storeScope: CoroutineScope,
) {
    private val _state = MutableStateFlow(EditorUiState())
    val state: StateFlow<EditorUiState> = _state

    private val ids = IdSource { UUID.randomUUID().toString() }

    /** The project the first edit created and no save has written yet; every other one has a folder. */
    @Volatile private var unsaved: ProjectId? = null

    /**
     * Opens project [id], or with no id the most recently changed one. With no projects at all the
     * state is an empty, unsaved edit (see [EditorUiState.id]).
     */
    fun open(id: ProjectId? = null) {
        scope.launch {
            val (listed, templates, loaded) =
                withContext(storeScope.coroutineContext) {
                    val all = store.list()
                    Triple(all, store.templates(), if (id == null) all.firstOrNull() else store.load(id))
                }
            show(loaded, listed, templates)
        }
    }

    /** A new project for [destination] (over [track] when one is given), saved and opened. */
    fun create(
        name: String? = null,
        destination: ExportDestination = ExportDestination.CUSTOM,
        track: TrackRef? = null,
        kind: ProjectKind = ProjectKind.Standard,
    ) {
        withStore { all ->
            val project =
                StudioProject.create(
                    id = store.newId(),
                    name = name ?: nextName(all),
                    destination = destination,
                    defaults = store.defaultsFor(destination),
                    nowMs = store.clock(),
                    ids = ids,
                    track = track,
                    kind = kind,
                )
            store.save(project)
            project
        }
    }

    /** "New from template": [templateId] fitted to [track], saved and opened. */
    fun createFromTemplate(
        templateId: ProjectId,
        track: TrackRef,
        name: String? = null,
    ) {
        withStore { all ->
            val template = store.loadTemplate(templateId) ?: return@withStore null
            val project = template.newProject(store.newId(), name ?: track.title.ifBlank { nextName(all) }, track, store.clock(), ids)
            store.save(project)
            project
        }
    }

    /**
     * A batch: one project per track from [templateId], all saved, none opened. [onCreated] gets
     * their ids in track order on the main thread, for the render path to queue.
     */
    fun createBatch(
        templateId: ProjectId,
        tracks: List<TrackRef>,
        onCreated: (List<ProjectId>) -> Unit = {},
    ) {
        scope.launch {
            val created =
                withContext(storeScope.coroutineContext) {
                    val template = store.loadTemplate(templateId) ?: return@withContext emptyList<ProjectId>()
                    template.batch(tracks, store.clock(), ids).onEach { store.save(it) }.map { it.id }
                }
            refreshLists()
            onCreated(created)
        }
    }

    /**
     * "New from what's playing": [track] on an audio lane and the look on stage ([sceneId], plus
     * [milkPath] for a MilkDrop preset) as one visual clip over the whole song, saved and opened.
     * With a [render] the project also lists that finished video: the "keep it as a project too"
     * answer of a one-tap Render, made after the file exists so a failed render leaves no folder.
     */
    fun createWithLook(
        track: TrackRef,
        sceneId: String,
        milkPath: String?,
        render: ProjectRender? = null,
    ) {
        withStore { all ->
            val base =
                StudioProject.create(
                    id = store.newId(),
                    name = track.title.ifBlank { nextName(all) },
                    destination = ExportDestination.CUSTOM,
                    defaults = store.defaultsFor(ExportDestination.CUSTOM),
                    nowMs = store.clock(),
                    ids = ids,
                    track = track,
                )
            val lengthMs = maxOf(track.durationMs, render?.durationMs ?: 0L)
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
            val project = base.copy(edit = base.edit.copy(timeline = timeline))
            (if (render == null) project else project.withRender(render)).also { store.save(it) }
        }
    }

    /**
     * "New from a video on the phone": the video on a media lane from 0, the programme its length,
     * saved and opened. [durationMs] comes from the clip's own metadata (`describeMedia`).
     */
    fun createFromVideo(
        uri: String,
        label: String,
        durationMs: Long,
    ) {
        withStore { all ->
            val base =
                StudioProject.create(
                    id = store.newId(),
                    name = label.ifBlank { nextName(all) },
                    destination = ExportDestination.CUSTOM,
                    defaults = store.defaultsFor(ExportDestination.CUSTOM),
                    nowMs = store.clock(),
                    ids = ids,
                )
            val clip =
                Clip(
                    id = ClipId(ids.next()),
                    content = ClipContent.Video(uri),
                    startMs = 0L,
                    durationMs = durationMs,
                    sourceDurationMs = durationMs,
                    label = label,
                )
            val media = Lane(id = LaneId(ids.next()), kind = LaneKind.Media, name = label, clips = listOf(clip))
            base.copy(edit = base.edit.copy(timeline = base.edit.timeline.copy(lanes = listOf(media), durationMs = durationMs)))
                .also { store.save(it) }
        }
    }

    fun rename(
        id: ProjectId,
        name: String,
    ) {
        val trimmed = name.trim()
        if (trimmed.isEmpty()) return
        if (_state.value.id == id) {
            updateFolder { it.copy(name = trimmed) }
            return
        }
        storeScope.launch {
            store.load(id)?.let { store.save(store.touch(it.copy(name = trimmed))) }
            refreshLists()
        }
    }

    /** Deletes a project folder; deleting the open one opens the next most recent. */
    fun delete(id: ProjectId) {
        val wasOpen = _state.value.id == id
        storeScope.launch {
            store.delete(id)
            if (wasOpen) open() else refreshLists()
        }
    }

    /** The open project's own export settings. Settings › Export is not touched. */
    fun setExport(export: ExportDefaults) = updateFolder { it.copy(export = export) }

    fun setKind(kind: ProjectKind) = updateFolder { it.copy(kind = kind) }

    /** Saves the open project minus its audio as a template called [name]. */
    fun saveAsTemplate(name: String) {
        val open = _state.value.open ?: return
        storeScope.launch {
            store.saveTemplate(open.toTemplate(store.newId(), name.trim().ifEmpty { open.name }, store.clock()))
            refreshLists()
        }
    }

    fun deleteTemplate(id: ProjectId) {
        storeScope.launch {
            store.deleteTemplate(id)
            refreshLists()
        }
    }

    /** Adds a finished video to project [id]'s list, open or not. */
    fun recordRender(
        id: ProjectId,
        render: ProjectRender,
    ) = changeProject(id) { it.withRender(render) }

    fun removeRender(
        id: ProjectId,
        uri: String,
    ) = changeProject(id) { it.withoutRender(uri) }

    fun edit(transform: (EditorProject) -> EditorProject) {
        val current = _state.value
        val next = current.history.push(transform(current.project))
        if (next === current.history) return
        val folder = current.folder ?: newFolder(current.projects)
        _state.value = current.copy(history = next, folder = folder, id = folder.id, name = folder.name)
        autosave(folder.copy(edit = next.present))
    }

    fun apply(result: EditResult) = edit { it.apply(result) }

    fun undo() = step(EditorHistory::undo)

    fun redo() = step(EditorHistory::redo)

    fun setPlayhead(ms: Long) {
        _state.update { it.copy(playheadMs = ms.coerceAtLeast(0L)) }
    }

    fun newClipId(): ClipId = ClipId(UUID.randomUUID().toString())

    fun newMarkerId(): MarkerId = MarkerId(UUID.randomUUID().toString())

    fun newKeyframeId(): KeyframeId = KeyframeId(UUID.randomUUID().toString())

    private fun step(move: (EditorHistory) -> EditorHistory) {
        val current = _state.value
        val next = move(current.history)
        if (next === current.history) return
        _state.value = current.copy(history = next)
        current.folder?.let { autosave(it.copy(edit = next.present)) }
    }

    private fun updateFolder(change: (StudioProject) -> StudioProject) {
        val current = _state.value
        val folder = change(current.folder ?: newFolder(current.projects))
        _state.value = current.copy(folder = folder, id = folder.id, name = folder.name)
        autosave(folder.copy(edit = current.project))
    }

    private fun changeProject(
        id: ProjectId,
        change: (StudioProject) -> StudioProject,
    ) {
        if (_state.value.id == id) {
            updateFolder(change)
            return
        }
        storeScope.launch {
            store.load(id)?.let { store.save(store.touch(change(it))) }
            refreshLists()
        }
    }

    /** The project the first edit of an empty Studio creates. Saved by the autosave that follows. */
    private fun newFolder(existing: List<StudioProject>): StudioProject {
        val folder =
            StudioProject.create(
                id = store.newId(),
                name = nextName(existing),
                destination = ExportDestination.CUSTOM,
                defaults = store.defaultsFor(ExportDestination.CUSTOM),
                nowMs = store.clock(),
                ids = ids,
            )
        unsaved = folder.id
        return folder
    }

    /**
     * Writes the open project and puts it first in [EditorUiState.projects] without re-reading every
     * folder. A folder deleted behind the controller's back (its take deleted from My videos) is not
     * written again: the controller opens the next project instead.
     */
    private fun autosave(project: StudioProject) {
        storeScope.launch {
            if (project.id != unsaved && !store.folderOf(project.id).isDirectory) {
                open()
                return@launch
            }
            val saved = store.touch(project)
            store.save(saved)
            if (unsaved == project.id) unsaved = null
            _state.update { s -> s.copy(projects = listOf(saved) + s.projects.filterNot { it.id == saved.id }) }
        }
    }

    /** Re-reads the project and template lists, for a screen that knows the disk changed (a new take). */
    fun refresh() {
        storeScope.launch { refreshLists() }
    }

    /** Runs [make] on the store with the current list, then opens what it made. */
    private fun withStore(make: (List<StudioProject>) -> StudioProject?) {
        scope.launch {
            val made = withContext(storeScope.coroutineContext) { make(store.list()) } ?: return@launch
            val (listed, templates) = withContext(storeScope.coroutineContext) { store.list() to store.templates() }
            show(made, listed, templates)
        }
    }

    private fun show(
        project: StudioProject?,
        listed: List<StudioProject>,
        templates: List<ProjectTemplate>,
    ) {
        _state.value =
            EditorUiState(
                id = project?.id,
                name = project?.name.orEmpty(),
                history = EditorHistory(project?.edit ?: EditorProject()),
                loaded = true,
                projects = listed,
                templates = templates,
                folder = project,
            )
    }

    private suspend fun refreshLists() {
        val listed = store.list()
        val templates = store.templates()
        _state.update { it.copy(projects = listed, templates = templates) }
    }

    private fun nextName(existing: List<StudioProject>): String {
        val taken = existing.map { it.name }.toSet()
        var n = existing.size + 1
        while ("$DEFAULT_NAME $n" in taken) n++
        return "$DEFAULT_NAME $n"
    }

    companion object {
        /** Prefix of the names new projects get ("Project 3") until the user names them. */
        const val DEFAULT_NAME = "Project"
    }
}
