package dev.geode.ui

import android.content.Intent
import android.net.Uri
import androidx.annotation.OptIn
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import androidx.media3.common.util.UnstableApi
import dagger.hilt.android.lifecycle.HiltViewModel
import dev.geode.audio.MicCapture
import dev.geode.data.Lyrics
import dev.geode.data.MilkPackImporter
import dev.geode.data.Preset
import dev.geode.data.VizUiState
import dev.geode.di.PlayerSessionProvider
import dev.geode.engine.bridge.GeodeNative
import dev.geode.export.ExportAspect
import dev.geode.export.ExportCodec
import dev.geode.export.ExportRange
import dev.geode.export.SceneFactory
import dev.geode.model.QueueTrack
import dev.geode.model.QueueUiState
import dev.geode.model.VizPlaylistEntry
import dev.geode.pro.FeatureGate
import dev.geode.pro.GetFeatureGateUseCase
import dev.geode.pro.PaywallNavigator
import dev.geode.pro.PaywallTrigger
import dev.geode.pro.ProFeature
import dev.geode.render.scene.SceneParams
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.stateIn
import javax.inject.Inject

@OptIn(UnstableApi::class)
@HiltViewModel
class PlayerViewModel
    @Inject
    constructor(
        private val sessions: PlayerSessionProvider,
        getFeatureGate: GetFeatureGateUseCase,
        private val paywall: PaywallNavigator,
    ) : ViewModel() {
        private val session: PlayerSession = sessions.get()

        /** What exports may do (resolution, frame rate, wordmark), and which styles are locked. */
        val gate: StateFlow<FeatureGate> = getFeatureGate().stateIn(viewModelScope, SharingStarted.Eagerly, FeatureGate.FREE)
        private val playback: PlaybackRepository = session.playbackRepository
        private val visualizer: VisualizerRepository = session.visualizerRepository

        val uiState: StateFlow<PlayerUiState> get() = playback.state

        val positionMs: StateFlow<Long> get() = playback.positionMs

        val queue: StateFlow<QueueUiState> get() = playback.queue

        val abLoop: StateFlow<AbLoop?> get() = playback.abLoop

        val playbackNotice: StateFlow<String?> get() = playback.notice

        val vizState: StateFlow<VizUiState> get() = visualizer.viz

        val activeMilkPath: StateFlow<String?> get() = visualizer.activeMilkPath

        val micState: StateFlow<MicState> get() = session.captureController.micState

        val externalAudio: StateFlow<ExternalAudioState> get() = session.externalAudio

        val historyTick: StateFlow<Int> get() = session.listening.historyTick

        val favourites: StateFlow<Set<String>> get() = session.favourites

        val artPaletteNote: StateFlow<String?> get() = session.trackColor.artPaletteNote

        val lyrics: StateFlow<Lyrics?> get() = session.lyrics

        fun attachLyrics(source: android.net.Uri) = session.attachLyrics(source)

        val sleepTimerRemainingMs: StateFlow<Long?> get() = session.sleepTimerRemainingMs

        val vizApply: SharedFlow<VizApply> get() = session.visual.vizApply

        val morphFade: SharedFlow<Float> get() = session.visual.morphFade

        fun setMicEnabled(enabled: Boolean): MicCapture.Failure? = session.setMicEnabled(enabled)

        fun onAppBackground() = session.captureController.onBackground()

        fun onAppForeground() = session.captureController.onForeground()

        fun hasMicPermission(): Boolean = session.captureController.hasMicPermission()

        fun noteExternalAudioConsentPending() = session.captureController.noteExternalAudioConsentPending()

        fun noteExternalAudioConsentDenied() = session.captureController.noteExternalAudioConsentDenied()

        fun stopExternalAudio() = session.stopExternalAudio()

        fun notificationAccessIntent(): Intent = session.captureController.notificationAccessIntent()

        fun toggleShuffle() = session.settings.toggleShuffle()

        fun cycleRepeatMode() = session.settings.cycleRepeatMode()

        fun importMilkPresetAsync(
            uri: Uri,
            onDone: (String?) -> Unit,
        ) = session.importMilkPresetAsync(uri, onDone)

        fun milkTextureLinksFor(path: String) = session.milkImport.textureLinksFor(path)

        fun assignMilkTextureAsync(
            path: String,
            expected: String,
            texture: String?,
            onDone: (List<dev.geode.data.MilkTextureLink>) -> Unit,
        ) = session.assignMilkTextureAsync(path, expected, texture, onDone)

        fun importMilkFolderAsync(
            treeUri: Uri,
            onDone: (MilkPackImporter.Report) -> Unit,
        ) = session.importMilkFolderAsync(treeUri, onDone)

        fun applyArtworkPalette() = session.trackColor.applyArtworkPalette()

        /** The playing track's artwork for the eyedropper, or null (nothing playing, no artwork). Off the main thread. */
        suspend fun artworkForPicking(): android.graphics.Bitmap? = session.trackColor.artworkBitmap()

        fun startSleepTimer(minutes: Int) = session.startSleepTimer(minutes)

        fun cancelSleepTimer() = session.cancelSleepTimer()

        fun addToVizPlaylist(entry: VizPlaylistEntry) = session.autoVisuals.addToVizPlaylist(entry)

        fun removeVizPlaylistAt(index: Int) = session.autoVisuals.removeVizPlaylistAt(index)

        fun setVizPlaylistEnabled(enabled: Boolean) = session.autoVisuals.setVizPlaylistEnabled(enabled)

        fun setVizPlaylistInterval(seconds: Int) = session.autoVisuals.setVizPlaylistInterval(seconds)

        fun setRandomEnabled(enabled: Boolean) = session.autoVisuals.setRandomEnabled(enabled)

        fun setRandomInterval(seconds: Int) = session.autoVisuals.setRandomInterval(seconds)

        fun setRandomIncludeStyles(enabled: Boolean) = session.autoVisuals.setRandomIncludeStyles(enabled)

        fun setRandomIncludePresets(enabled: Boolean) = session.autoVisuals.setRandomIncludePresets(enabled)

        fun setRandomIncludeMilk(enabled: Boolean) = session.autoVisuals.setRandomIncludeMilk(enabled)

        fun onDirectorStyleMoment() = session.autoVisuals.onDirectorMoment()

        /** Customize is open or closed; Auto holds its style changes while it is open. */
        fun setCustomizing(open: Boolean) {
            session.autoVisuals.editing = open
        }

        fun stepVizPlaylist(step: Int = 1): Boolean = session.autoVisuals.stepVizPlaylist(step)

        fun recentlyPlayed() = session.listening.recentlyPlayed()

        fun currentTrackUri(): String? = session.currentTrackUri()

        fun toggleFavourite(uri: String? = null) = session.toggleFavourite(uri)

        fun cycleAbLoop() = session.queueController.cycleAbLoop()

        fun removeQueueItem(index: Int) = session.queueController.removeQueueItem(index)

        fun moveQueueItem(
            from: Int,
            to: Int,
        ) = session.queueController.moveQueueItem(from, to)

        fun resumeLastPlayed() = session.resumeLastPlayed()

        fun playNext(uri: String) = session.queueController.playNext(uri)

        fun playFromSearch(query: String) = session.playFromSearch(query)

        /** An audio file another app opened; [onResult] is false when it was refused. */
        fun openAudio(
            uri: Uri,
            onResult: (Boolean) -> Unit,
        ) = session.openAudio(uri, onResult)

        fun enqueue(uri: String) = session.queueController.enqueue(uri)

        fun shuffleAllHistory() = session.shuffleAllHistory()

        fun playTrack(uri: String) = session.playTrack(uri)

        fun playFrom(
            tracks: List<QueueTrack>,
            startUri: String,
        ) = session.playFrom(tracks, startUri)

        fun playAll(
            tracks: List<QueueTrack>,
            shuffled: Boolean = false,
        ) = session.playAll(tracks, shuffled)

        fun open(uris: List<Uri>) = session.queueController.open(uris)

        fun playQueueIndex(index: Int) = session.queueController.playQueueIndex(index)

        fun next() = session.queueController.next()

        fun previous() = session.queueController.previous()

        fun togglePlayPause() = session.togglePlayPause()

        fun seekToMs(positionMs: Long) = session.queueController.seekToMs(positionMs)

        fun seekTo(fraction: Float) = session.queueController.seekTo(fraction)

        fun clearPlaybackNotice() = session.clearPlaybackNotice()

        fun selectScene(sceneId: String) = session.selectScene(sceneId)

        fun previewScene(sceneId: String) = session.previewScene(sceneId)

        fun leaveScenePreview() = session.leaveScenePreview()

        fun setReactivity(
            attack: Float,
            decay: Float,
        ) = session.visual.setReactivity(attack, decay)

        /** Settings › Gestures & controls › Touch › Pinch and twist: a pinch's factor on Zoom, a twist's degrees on Rotation. */
        fun nudgeTransform(
            zoomFactor: Float,
            degrees: Float,
        ) = session.visual.nudgeTransform(zoomFactor, degrees)

        fun reportShaderError(error: String?) = session.visual.reportShaderError(error)

        fun reportLiquidMode(mode: Int) = session.reportLiquidMode(mode)

        fun applyCustomShader(source: String) = session.visual.applyCustomShader(source)

        fun setTransitionId(id: String) = session.visual.setTransitionId(id)

        fun setTransitionDuration(seconds: Float) = session.visual.setTransitionDuration(seconds)

        fun applyPreset(preset: Preset) = session.applyPreset(preset)

        fun startExport(
            aspect: ExportAspect,
            fps: Int,
            sceneFactory: SceneFactory,
            destination: Uri? = null,
            range: ExportRange? = null,
            codec: ExportCodec = ExportCodec.H264,
        ) {
            if (openPaywallIfLocked(aspect, fps)) return
            val limits = gate.value
            val capped = capRange(range, limits.exportCapMs)
            session.startExport(aspect, fps, sceneFactory, destination, capped, codec, pinnedWatermark())
        }

        /**
         * A batch render (play queue, a playlist, chosen tracks) is Pro, under the same feature as
         * loop renders: free and trial users keep single renders of the current track.
         */
        fun startBatch(
            tracks: List<BatchTrack>,
            aspect: ExportAspect,
            fps: Int,
            sceneFactory: SceneFactory,
            sceneParams: SceneParams?,
            codec: ExportCodec,
        ) {
            if (openPaywallIfLocked(aspect, fps)) return
            if (!gate.value.allows(ProFeature.FULL_LENGTH_EXPORT)) {
                paywall.open(PaywallTrigger.Feature(ProFeature.FULL_LENGTH_EXPORT))
                return
            }
            session.exportController.startBatch(tracks, aspect, fps, sceneFactory, sceneParams, codec, pinnedWatermark())
        }

        fun startLoopRender(
            aspect: ExportAspect,
            codec: ExportCodec,
            fps: Int,
            sceneFactory: SceneFactory,
            loopMs: Long,
            crossfadeMs: Long,
            drift: dev.geode.export.TimeOfDayDrift,
            audioClips: List<Uri>,
            destination: Uri? = null,
        ) {
            if (openPaywallIfLocked(aspect, fps)) return
            // A loop render is a long-form video by construction, so it is the full-length tier.
            if (!gate.value.allows(ProFeature.FULL_LENGTH_EXPORT)) {
                paywall.open(PaywallTrigger.Feature(ProFeature.FULL_LENGTH_EXPORT))
                return
            }
            session.exportController.startLoopRender(
                aspect,
                codec,
                fps,
                sceneFactory,
                loopMs,
                crossfadeMs,
                drift,
                audioClips,
                destination,
                pinnedWatermark(),
            )
        }

        /** The entitlement's wordmark, placed as Settings › Preferences says now; the render keeps this one. */
        private fun pinnedWatermark() =
            gate.value.renderLimits
                .withMark(session.watermarkPrefs.value.value)
                .watermark

        /**
         * Without Pro an export stops at [capMs]: the requested range, shortened, or the track's first
         * minute. The audio transcode stops at the range's end, so a shorter track is unaffected.
         */
        private fun capRange(
            range: ExportRange?,
            capMs: Long?,
        ): ExportRange? {
            val out = LongArray(2)
            GeodeNative.exportRangeCapped(range?.startMs ?: 0L, range?.durationMs ?: 0L, capMs ?: 0L, out)
            return if (out[1] > 0) ExportRange(out[0], out[1]) else null
        }

        /** 4K and 60 fps are Pro: a locked size or rate opens the paywall instead of rendering. */
        private fun openPaywallIfLocked(
            aspect: ExportAspect,
            fps: Int,
        ): Boolean {
            val blocking = gate.value.blockingExportFeature(minOf(aspect.width, aspect.height), fps) ?: return false
            paywall.open(PaywallTrigger.Feature(blocking))
            return true
        }

        /**
         * This ViewModel owns the session's teardown. `AppRoot` creates it unconditionally and
         * every Geode ViewModel shares the Activity's store, so this runs exactly once, when the
         * whole UI is going away — see [dev.geode.di.PlayerSessionProvider].
         */
        override fun onCleared() {
            sessions.shutdown()
        }
    }
