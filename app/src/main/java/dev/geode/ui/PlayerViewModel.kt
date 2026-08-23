package dev.geode.ui

import android.content.Intent
import android.net.Uri
import androidx.annotation.OptIn
import androidx.lifecycle.ViewModel
import androidx.media3.common.util.UnstableApi
import androidx.media3.exoplayer.ExoPlayer
import dagger.hilt.android.lifecycle.HiltViewModel
import dev.geode.analysis.AudioFeatures
import dev.geode.analysis.IntelligenceMode
import dev.geode.analysis.LiveInputProfile
import dev.geode.audio.MicCapture
import dev.geode.data.MilkPackImporter
import dev.geode.data.Preset
import dev.geode.di.PlayerSessionProvider
import dev.geode.export.ExportAspect
import dev.geode.export.ExportRange
import dev.geode.render.SceneFactory
import dev.geode.render.TransitionStyle
import dev.geode.render.scene.PcmChunk
import dev.geode.render.scene.SceneParams
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import javax.inject.Inject

@OptIn(UnstableApi::class)
@HiltViewModel
class PlayerViewModel
    @Inject
    constructor(
        private val sessions: PlayerSessionProvider,
    ) : ViewModel() {
        private val session: PlayerSession = sessions.get()
        private val playback: PlaybackRepository = session.playbackRepository
        private val visualizer: VisualizerRepository = session.visualizerRepository

        val player: ExoPlayer get() = session.player

        val uiState: StateFlow<PlayerUiState> get() = playback.state

        val queue: StateFlow<QueueUiState> get() = playback.queue

        val abLoop: StateFlow<AbLoop?> get() = playback.abLoop

        val playbackNotice: StateFlow<String?> get() = playback.notice

        val vizState: StateFlow<VizUiState> get() = visualizer.viz

        val features: StateFlow<AudioFeatures> get() = visualizer.features

        val waveform: StateFlow<FloatArray?> get() = visualizer.waveform

        val activeMilkPath: StateFlow<String?> get() = visualizer.activeMilkPath

        val micState: StateFlow<MicState> get() = session.micState

        val externalAudio: StateFlow<ExternalAudioState> get() = session.externalAudio

        val historyTick: StateFlow<Int> get() = session.historyTick

        val favourites: StateFlow<Set<String>> get() = session.favourites

        val artPaletteNote: StateFlow<String?> get() = session.artPaletteNote

        val lyrics: StateFlow<Lyrics?> get() = session.lyrics

        val autoMode: StateFlow<Int> get() = session.autoMode

        val sleepTimerRemainingMs: StateFlow<Long?> get() = session.sleepTimerRemainingMs

        val vizApply: SharedFlow<VizApply> get() = session.vizApply

        val morphFade: SharedFlow<Float> get() = session.morphFade

        fun setMicEnabled(enabled: Boolean): MicCapture.Failure? = session.setMicEnabled(enabled)

        fun hasMicPermission(): Boolean = session.hasMicPermission()

        fun noteExternalAudioConsentPending() = session.noteExternalAudioConsentPending()

        fun noteExternalAudioConsentDenied() = session.noteExternalAudioConsentDenied()

        fun stopExternalAudio() = session.stopExternalAudio()

        fun notificationAccessIntent(): Intent = session.notificationAccessIntent()

        fun toggleShuffle() = session.toggleShuffle()

        fun cycleRepeatMode() = session.cycleRepeatMode()

        fun applyLiveInputProfile(profile: LiveInputProfile) = session.applyLiveInputProfile(profile)

        fun enrichFeatures(f: AudioFeatures): AudioFeatures = session.enrichFeatures(f)

        fun latestPcm(): PcmChunk? = session.latestPcm()

        fun milkPresetFilesAsync(onDone: (List<MilkFile>) -> Unit) = session.milkPresetFilesAsync(onDone)

        fun importMilkPresetAsync(
            uri: Uri,
            onDone: (String?) -> Unit,
        ) = session.importMilkPresetAsync(uri, onDone)

        internal fun importMilkPresetBlocking(uri: Uri): String? = session.importMilkPresetBlocking(uri)

        fun milkTextureLinksFor(path: String) = session.milkTextureLinksFor(path)

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

        fun applyKeyColor(key: String) = session.applyKeyColor(key)

        fun setKeyColor(enabled: Boolean) = session.setKeyColor(enabled)

        fun applyArtworkPalette() = session.applyArtworkPalette()

        fun currentTrackKey(): String? = session.currentTrackKey()

        fun setIntelligenceMode(mode: IntelligenceMode) = session.setIntelligenceMode(mode)

        fun analyzeCurrentTrack() = session.analyzeCurrentTrack()

        fun startSleepTimer(minutes: Int) = session.startSleepTimer(minutes)

        fun cancelSleepTimer() = session.cancelSleepTimer()

        fun addToVizPlaylist(entry: VizPlaylistEntry) = session.addToVizPlaylist(entry)

        fun removeVizPlaylistAt(index: Int) = session.removeVizPlaylistAt(index)

        fun setVizPlaylistEnabled(enabled: Boolean) = session.setVizPlaylistEnabled(enabled)

        fun setVizPlaylistIntelligent(enabled: Boolean) = session.setVizPlaylistIntelligent(enabled)

        fun setVizPlaylistInterval(seconds: Int) = session.setVizPlaylistInterval(seconds)

        fun setRandomEnabled(enabled: Boolean) = session.setRandomEnabled(enabled)

        fun setRandomInterval(seconds: Int) = session.setRandomInterval(seconds)

        fun setRandomOnBeat(enabled: Boolean) = session.setRandomOnBeat(enabled)

        fun setRandomIncludeStyles(enabled: Boolean) = session.setRandomIncludeStyles(enabled)

        fun setRandomIncludePresets(enabled: Boolean) = session.setRandomIncludePresets(enabled)

        fun setRandomIncludeMilk(enabled: Boolean) = session.setRandomIncludeMilk(enabled)

        fun setRandomizeColors(enabled: Boolean) = session.setRandomizeColors(enabled)

        fun setSectionStaging(enabled: Boolean) = session.setSectionStaging(enabled)

        fun randomStepNow() = session.randomStepNow()

        fun applyVizEntry(entry: VizPlaylistEntry) = session.applyVizEntry(entry)

        fun recentlyPlayed() = session.recentlyPlayed()

        fun currentTrackUri(): String? = session.currentTrackUri()

        fun toggleFavourite(uri: String? = null) = session.toggleFavourite(uri)

        fun cycleAbLoop() = session.cycleAbLoop()

        fun clearAbLoop() = session.clearAbLoop()

        fun removeQueueItem(index: Int) = session.removeQueueItem(index)

        fun moveQueueItem(
            from: Int,
            to: Int,
        ) = session.moveQueueItem(from, to)

        fun togglePlayPauseFaded() = session.togglePlayPauseFaded()

        fun resumeLastPlayed() = session.resumeLastPlayed()

        fun cycleAutoMode() = session.cycleAutoMode()

        fun playNext(uri: String) = session.playNext(uri)

        fun enqueue(uri: String) = session.enqueue(uri)

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

        fun open(uris: List<Uri>) = session.open(uris)

        fun queueTitles(): List<String> = session.queueTitles()

        fun playQueueIndex(index: Int) = session.playQueueIndex(index)

        fun next() = session.next()

        fun previous() = session.previous()

        fun togglePlayPause() = session.togglePlayPause()

        fun seekToMs(positionMs: Long) = session.seekToMs(positionMs)

        fun seekTo(fraction: Float) = session.seekTo(fraction)

        fun clearPlaybackNotice() = session.clearPlaybackNotice()

        fun selectScene(sceneId: String) = session.selectScene(sceneId)

        fun setReactivity(
            attack: Float,
            decay: Float,
        ) = session.setReactivity(attack, decay)

        fun setSceneParams(params: SceneParams) = session.setSceneParams(params)

        fun resetSceneParams() = session.resetSceneParams()

        fun nudgeTransform(
            zoomFactor: Float,
            rotationDegrees: Float,
        ) = session.nudgeTransform(zoomFactor, rotationDegrees)

        fun reportShaderError(error: String?) = session.reportShaderError(error)

        fun applyCustomShader(source: String) = session.applyCustomShader(source)

        fun setTransitionStyle(style: TransitionStyle) = session.setTransitionStyle(style)

        fun setTransitionId(id: String) = session.setTransitionId(id)

        fun setTransitionDuration(seconds: Float) = session.setTransitionDuration(seconds)

        fun applyPreset(preset: Preset) = session.applyPreset(preset)

        fun startExport(
            aspect: ExportAspect,
            fps: Int,
            sceneFactory: SceneFactory,
            destination: Uri? = null,
            loopSafe: Boolean = false,
            range: ExportRange? = null,
            sceneFactoryFor: ((String) -> SceneFactory)? = null,
        ) = session.startExport(aspect, fps, sceneFactory, destination, loopSafe, range, sceneFactoryFor)

        /**
         * This ViewModel owns the session's teardown. `AppRoot` creates it unconditionally and
         * every Geode ViewModel shares the Activity's store, so this runs exactly once, when the
         * whole UI is going away — see [dev.geode.di.PlayerSessionProvider].
         */
        override fun onCleared() {
            sessions.shutdown()
        }
    }
