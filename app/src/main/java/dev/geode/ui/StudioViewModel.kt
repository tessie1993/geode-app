package dev.geode.ui

import android.net.Uri
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import dagger.hilt.android.lifecycle.HiltViewModel
import dev.geode.di.PlayerSessionProvider
import dev.geode.editor.ClipId
import dev.geode.editor.EditResult
import dev.geode.editor.EditorProject
import dev.geode.editor.KeyframeId
import dev.geode.editor.MarkerId
import dev.geode.editor.SubtitleCue
import dev.geode.editor.Subtitles
import dev.geode.editor.TransientEnvelope
import dev.geode.editor.TransientSource
import dev.geode.export.ClipEdit
import dev.geode.export.StudioClip
import dev.geode.pro.FeatureGate
import dev.geode.pro.GetFeatureGateUseCase
import dev.geode.pro.PaywallNavigator
import dev.geode.pro.PaywallTrigger
import dev.geode.pro.ProFeature
import dev.geode.render.scene.SceneParams
import dev.geode.ui.studio.EditorActions
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.stateIn
import javax.inject.Inject

@HiltViewModel
class StudioViewModel
    @Inject
    constructor(
        private val sessions: PlayerSessionProvider,
        getFeatureGate: GetFeatureGateUseCase,
        private val paywall: PaywallNavigator,
    ) : ViewModel(),
        EditorActions {
        private val session: PlayerSession = sessions.get()

        /**
         * Studio exports go through Media3 Transformer, which has no free-tier cap or wordmark, so
         * they need the full Pro tier: the trial edits in the Studio, and exporting asks to unlock.
         */
        private val gate: StateFlow<FeatureGate> = getFeatureGate().stateIn(viewModelScope, SharingStarted.Eagerly, FeatureGate.FREE)

        private fun studioExportLocked(): Boolean {
            if (gate.value.allows(ProFeature.FULL_LENGTH_EXPORT) && gate.value.allows(ProFeature.NO_WATERMARK)) return false
            paywall.open(PaywallTrigger.Feature(ProFeature.FULL_LENGTH_EXPORT))
            return true
        }
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

        override fun exportProject(destination: Uri?) {
            if (studioExportLocked()) return
            session.startProjectExport(destination)
        }

        override fun cancelProjectExport() = session.cancelStudioExport()

        override fun lyricCues(): List<SubtitleCue>? =
            session.lyrics.value
                ?.takeIf { it.synced }
                ?.let { Subtitles.fromLyrics(it.lines, session.trackDurationMs()) }
                ?.takeIf { it.isNotEmpty() }

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
            destination: Uri? = null,
        ) {
            if (studioExportLocked()) return
            session.startStudioExport(clip, edit, destination)
        }

        fun cancelStudioExport() = session.cancelStudioExport()

        fun clearStudioResult() = session.clearStudioResult()

        val loopState: StateFlow<LoopUiState> get() = session.loopState

        fun cancelLoopRender() = session.cancelLoopRender()

        fun clearLoopResult() = session.clearLoopResult()
    }
