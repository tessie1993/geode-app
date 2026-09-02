package dev.geode.ui

import android.net.Uri
import androidx.lifecycle.ViewModel
import dagger.hilt.android.lifecycle.HiltViewModel
import dev.geode.di.PlayerSessionProvider
import dev.geode.editor.ClipId
import dev.geode.editor.EditResult
import dev.geode.editor.EditorProject
import dev.geode.editor.KeyframeId
import dev.geode.editor.MarkerId
import dev.geode.editor.TransientEnvelope
import dev.geode.editor.TransientSource
import dev.geode.export.ClipEdit
import dev.geode.export.StudioClip
import dev.geode.render.scene.SceneParams
import dev.geode.ui.studio.EditorActions
import kotlinx.coroutines.flow.StateFlow
import javax.inject.Inject

@HiltViewModel
class StudioViewModel
    @Inject
    constructor(
        private val sessions: PlayerSessionProvider,
    ) : ViewModel(),
        EditorActions {
        private val session: PlayerSession = sessions.get()
        val studio: StateFlow<StudioUiState> get() = session.studio

        val editor: StateFlow<EditorUiState> get() = session.editor.state

        override fun edit(transform: (EditorProject) -> EditorProject) = session.editor.edit(transform)

        override fun apply(result: EditResult) = session.editor.apply(result)

        override fun undo() = session.editor.undo()

        override fun redo() = session.editor.redo()

        override fun setPlayhead(ms: Long) = session.editor.setPlayhead(ms)

        override fun newClipId(): ClipId = session.editor.newClipId()

        override fun newMarkerId(): MarkerId = session.editor.newMarkerId()

        override fun newKeyframeId(): KeyframeId = session.editor.newKeyframeId()

        override fun transientEnvelope(source: TransientSource): TransientEnvelope? =
            session.analysisTimeline()?.let { TransientEnvelope.from(it, source) }

        override fun currentSceneId(): String = session.currentSceneId()

        override fun currentSceneParams(): SceneParams = session.currentSceneParams()

        override fun playbackPositionMs(): Long? = session.playbackPositionMs()

        override fun describeMedia(
            uri: Uri,
            onReady: (StudioClip) -> Unit,
        ) = session.describeStudioClip(uri, onReady)

        override fun exportProject() = session.startProjectExport()

        override fun cancelProjectExport() = session.cancelStudioExport()

        val exportState: StateFlow<ExportUiState> get() = session.exportState

        val takeState: StateFlow<TakeUiState> get() = session.takeState

        fun startRecording() = session.startRecording()

        fun stopRecording(name: String? = null) = session.stopRecording(name)

        fun playTake(name: String) = session.playTake(name)

        fun stopReplay() = session.stopReplay()

        fun deleteTake(name: String) = session.deleteTake(name)

        fun renameTake(
            from: String,
            to: String,
        ) = session.renameTake(from, to)

        fun setExportTake(name: String?) = session.setExportTake(name)

        fun cancelExport() = session.cancelExport()

        fun resetExportState() = session.resetExportState()

        fun refreshStudioClips() = session.refreshStudioClips()

        fun describeStudioClip(
            uri: Uri,
            onReady: (StudioClip) -> Unit,
        ) = session.describeStudioClip(uri, onReady)

        fun renameStudioClip(
            uri: String,
            name: String,
            onResult: (Boolean) -> Unit,
        ) = session.renameStudioClip(uri, name, onResult)

        fun deleteStudioClip(
            uri: String,
            onResult: (Boolean) -> Unit,
        ) = session.deleteStudioClip(uri, onResult)

        fun startStudioExport(
            clip: StudioClip,
            edit: ClipEdit,
        ) = session.startStudioExport(clip, edit)

        fun cancelStudioExport() = session.cancelStudioExport()

        fun clearStudioResult() = session.clearStudioResult()
    }
