package dev.geode.ui

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import dagger.hilt.android.lifecycle.HiltViewModel
import dev.geode.data.SaveTarget
import dev.geode.di.PlayerSessionProvider
import dev.geode.editor.ProjectId
import dev.geode.editor.ProjectRender
import dev.geode.editor.TrackRef
import dev.geode.render.VisualizerView
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.dropWhile
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import javax.inject.Inject

/**
 * Studio's two tabs, Render and My videos, plus Record. Studio has no editor (the owner dropped
 * video editing from the launch, #227): a project here is only what a recording or a kept render
 * is stored as, so [editor] is read for its list of projects and their renders, never edited.
 */
@HiltViewModel
class StudioViewModel
    @Inject
    constructor(
        private val sessions: PlayerSessionProvider,
    ) : ViewModel() {
        private val session: PlayerSession = sessions.get()

        /** The project folders (recordings and kept renders), for My videos and the Render bar. */
        val editor: StateFlow<EditorUiState> get() = session.editor.state

        val exportState: StateFlow<ExportUiState> get() = session.exportController.exportState

        val recordState: StateFlow<RecordUiState> get() = session.record.state

        fun startRecording() = session.record.start()

        fun stopRecording() = session.record.stop()

        /** The stage Record copies from, while it is on screen; null when it goes. */
        fun attachRecordStage(stage: VisualizerView?) = session.record.attach(stage)

        /** The ask-once save sheet's answer for the recording waiting in [RecordUiState.pendingSave]. */
        fun chooseRecordSaveTarget(
            target: SaveTarget,
            remember: Boolean,
        ) = session.record.chooseSaveTarget(target, remember)

        /** The save sheet was dismissed: the recording goes to the Videos library this once. */
        fun skipRecordSave() = session.record.skipPendingSave()

        fun cancelExport() = session.exportController.cancelExport()

        fun resetExportState() = session.exportController.resetExportState()

        val loopState: StateFlow<LoopUiState> get() = session.exportController.loopState

        fun cancelLoopRender() = session.exportController.cancelLoopRender()

        fun clearLoopResult() = session.exportController.clearLoopResult()

        // ---------------------------------------------------------------- Render

        /** The song playing now, or null when nothing is loaded; [title] is what the player shows. */
        fun currentTrack(title: String?): TrackRef? =
            session.currentTrackUri()?.let { TrackRef(it, title.orEmpty(), session.trackDurationMs()) }

        // ---------------------------------------------------------------- My videos

        /** Takes the video off its project's list and deletes the file itself. */
        fun deleteRender(
            projectId: ProjectId,
            uri: String,
        ) {
            session.editor.removeRender(projectId, uri)
            session.deleteStudioClip(uri) {}
        }

        /**
         * My videos' deletes waiting out their Undo snackbar. They wait in [viewModelScope] (the
         * Activity's store), not in the tab's composition, so leaving the tab mid-snackbar still
         * deletes the item (#322) instead of bringing it back.
         */
        private val videoDeletes =
            PendingDeletes<VideoDelete>(viewModelScope, keyOf = VideoDelete::key) { deleteRender(it.projectId, it.uri) }

        /** Keys of My videos items hidden while their Undo is offered. */
        val pendingVideoDeletes: StateFlow<Set<String>> get() = videoDeletes.pending

        /** Hides the item now and deletes it once the Undo window passes, on screen or not. */
        fun deleteVideoLater(item: VideoDelete) = videoDeletes.delete(item)

        /** Undo on the snackbar: the item comes back and is never deleted. */
        fun undoVideoDelete(key: String) = videoDeletes.undo(key)

        // ---------------------------------------------------------------- Render's "keep as a project"

        private var keepJob: Job? = null

        /**
         * The user answered "keep it as a project too" for the render about to start: once that
         * render finishes, the song and [sceneId]'s look become a project listing the new file. A
         * render that fails or is cancelled leaves nothing behind; a second render replaces the wish.
         */
        fun keepNextRenderAsProject(
            track: TrackRef,
            sceneId: String,
            milkPath: String?,
        ) {
            keepJob?.cancel()
            keepJob =
                viewModelScope.launch {
                    // The render about to start resets the state first; wait for it to run, then end.
                    val exportState = session.exportController.exportState
                    val ended = exportState.dropWhile { !it.phase.isBusy }.first { !it.phase.isBusy }
                    val done = ended.phase as? ExportPhase.Done ?: return@launch
                    val render = ProjectRender(done.resultUri.toString(), track.title, System.currentTimeMillis(), track.durationMs)
                    session.editor.createWithLook(track, sceneId, milkPath, render)
                }
        }
    }

/** A My videos item to delete: a video's file and the project that lists it. [key] matches the grid's item key. */
data class VideoDelete(
    val projectId: ProjectId,
    val uri: String,
) {
    val key: String get() = keyOf(uri)

    companion object {
        fun keyOf(uri: String): String = "render:$uri"
    }
}
