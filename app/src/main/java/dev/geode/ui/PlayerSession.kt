package dev.geode.ui

import android.app.Application
import android.content.Intent
import android.net.Uri
import androidx.annotation.OptIn
import androidx.media3.common.PlaybackException
import androidx.media3.common.Player
import androidx.media3.common.util.UnstableApi
import androidx.media3.common.util.Util
import dev.geode.audio.AudioFxState
import dev.geode.audio.MicCapture
import dev.geode.data.BackgroundPrefs
import dev.geode.data.BackgroundPrefsStore
import dev.geode.data.DeviceTrackQuery
import dev.geode.data.EditorProjectStore
import dev.geode.data.FavouritesRepository
import dev.geode.data.FilePresetRepository
import dev.geode.data.FileSessionRepository
import dev.geode.data.FileTakeRepository
import dev.geode.data.FileTemplateRepository
import dev.geode.data.GuiPrefs
import dev.geode.data.LfoStore
import dev.geode.data.MilkPackImporter
import dev.geode.data.MilkTexture
import dev.geode.data.PlayerPrefs
import dev.geode.data.PlayerPrefsRepository
import dev.geode.data.PlayerPrefsStore
import dev.geode.data.Preset
import dev.geode.data.PresetFolders
import dev.geode.data.PresetRepository
import dev.geode.data.PresetStore
import dev.geode.data.SessionRepository
import dev.geode.data.SessionStore
import dev.geode.data.SharedPrefsFavouritesRepository
import dev.geode.data.SharedPrefsPlayerPrefsRepository
import dev.geode.data.SmartPlaylist
import dev.geode.data.SmartPlaylistMatcher
import dev.geode.data.TagWriteOutcome
import dev.geode.data.TakeStore
import dev.geode.data.TemplateId
import dev.geode.data.TemplateImport
import dev.geode.data.TemplateLook
import dev.geode.data.TemplateRepository
import dev.geode.data.TemplateWrite
import dev.geode.data.UserDataRepository
import dev.geode.data.VideoTemplate
import dev.geode.export.BatchTrack
import dev.geode.export.ExportAspect
import dev.geode.export.ExportCodec
import dev.geode.export.ExportRange
import dev.geode.export.StudioClip
import dev.geode.geodeContainer
import dev.geode.model.DeviceTrack
import dev.geode.model.LibraryTrack
import dev.geode.playback.BitPerfectOutput
import dev.geode.playback.PlaybackEngine
import dev.geode.playback.PlaybackErrors
import dev.geode.playback.PlaybackService
import dev.geode.render.AdsrConfig
import dev.geode.render.LfoConfig
import dev.geode.render.MilkPresetMemory
import dev.geode.render.SceneFactory
import dev.geode.render.TransitionStyle
import dev.geode.render.UnderlayBlend
import dev.geode.render.scene.CustomizeTab
import dev.geode.render.scene.PcmChunk
import dev.geode.render.scene.SceneParams
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.job
import kotlinx.coroutines.joinAll
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeoutOrNull
import java.io.File

@OptIn(UnstableApi::class)
class PlayerSession internal constructor(
    private val application: Application,
) {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)

    private val playback = PlaybackEngine.acquireForUi(application)

    private val ring = playback.ring

    @OptIn(ExperimentalCoroutinesApi::class)
    private val storeScope = CoroutineScope(SupervisorJob() + Dispatchers.IO.limitedParallelism(1))

    private val container = application.geodeContainer

    private val prefsFiles = container.prefsFiles

    private val playerPrefsRepository: PlayerPrefsRepository =
        SharedPrefsPlayerPrefsRepository(PlayerPrefsStore(prefsFiles.player), storeScope)

    private val favouritesRepository: FavouritesRepository =
        SharedPrefsFavouritesRepository(prefsFiles.favourites, storeScope)

    private val sessionRepository: SessionRepository = FileSessionRepository(SessionStore(application))

    private val presetRepository: PresetRepository = FilePresetRepository(PresetStore(application))

    private val captureController: CaptureController =
        CaptureController(
            application,
            scope,
            playback.captureSink,
            object : CaptureController.Host {
                override fun pausePlayback() = player.pause()

                override fun resetSound() {
                    playback.sampleRing.beginEpoch()
                }

                override fun setSoundRate(rateHz: Int) {
                    playback.sampleRateHz = rateHz
                }

                override fun setMicReactivePref(on: Boolean) {
                    setGuiPrefs(guiPrefs.value.copy(micReactive = on))
                }

                override val micReactivePref: Boolean get() = guiPrefs.value.micReactive
            },
        )

    val micState: StateFlow<MicState> get() = captureController.micState

    val externalAudio: StateFlow<ExternalAudioState> get() = captureController.externalAudio

    fun setMicEnabled(enabled: Boolean): MicCapture.Failure? = captureController.setMicEnabled(enabled)

    fun onAppBackground() = captureController.onBackground()

    fun onAppForeground() = captureController.onForeground()

    fun hasMicPermission(): Boolean = captureController.hasMicPermission()

    fun noteExternalAudioConsentPending() = captureController.noteExternalAudioConsentPending()

    fun noteExternalAudioConsentDenied() = captureController.noteExternalAudioConsentDenied()

    fun stopExternalAudio() = captureController.stopExternalAudio()

    fun notificationAccessIntent(): Intent = captureController.notificationAccessIntent()

    init {
        playback.onAudioFormat = captureController.audioFormatHook
    }

    private val presetLibrary: PresetLibraryController =
        PresetLibraryController(
            application,
            presetRepository,
            scope,
            storeScope,
            object : PresetLibraryController.Host {
                override val vizState: StateFlow<VizUiState> get() = _vizState

                override fun updatePresets(transform: (List<Preset>) -> List<Preset>) {
                    _vizState.update { it.copy(presets = transform(it.presets)) }
                }

                override val presetMirrorUri: String? get() = guiPrefs.value.presetMirrorUri
                override val activeMilkPath: String? get() = vizStateStore.activeMilkPath.value
            },
        )

    private val templateRepository: TemplateRepository = FileTemplateRepository.from(application)

    private val templateLibrary: TemplateController =
        TemplateController(
            application,
            templateRepository,
            scope,
            storeScope,
            object : TemplateController.Host {
                override val vizState: StateFlow<VizUiState> get() = _vizState
                override val activeMilkPath: String? get() = vizStateStore.activeMilkPath.value

                override fun applyLook(
                    look: TemplateLook,
                    name: String,
                ) = visual.applyPreset(look.toPreset(name))
            },
        )

    private val autoVisualsPrefsStore = AutoVisualsPrefsStore(prefsFiles.viz)
    private val vizStateStore = VizStateStore(prefsFiles.viz, scope, autoVisualsPrefsStore, MilkPresetMemory.shared(application))
    private val audioFxController = playback.audioFx

    private val settings: PlayerSettingsController =
        PlayerSettingsController(
            container.userData,
            playerPrefsRepository,
            container.appScope,
            playback.player,
            playback.audioFx,
            playback.replayGain,
            object : PlayerSettingsController.Host {
                override fun refreshUi() = refresh()

                override fun applyBitPerfect(enabled: Boolean) {
                    BitPerfectOutput.apply(application, enabled)
                }
            },
        )

    private val visual: VisualSettingsController =
        VisualSettingsController(
            vizStateStore,
            object : VisualSettingsController.Host {
                override val guiPrefs: GuiPrefs get() = settings.guiPrefs.value

                override fun milkPathFor(preset: Preset): String? = presetLibrary.milkPresetPathFor(preset)
            },
        )

    val player: Player = playback.player

    private val _uiState = MutableStateFlow(PlayerUiState())
    val uiState: StateFlow<PlayerUiState> = _uiState

    private val _positionMs = MutableStateFlow(0L)
    val positionMs: StateFlow<Long> = _positionMs

    private val _vizState get() = vizStateStore.state
    val vizState: StateFlow<VizUiState> get() = vizStateStore.state

    val exportState: StateFlow<ExportUiState> get() = exportController.exportState

    private val musicLibrary = MusicLibraryController(application, prefsFiles.library, scope)

    val library: StateFlow<LibraryState> get() = musicLibrary.library

    val trackOverrides: StateFlow<Map<String, LibraryTrack>> get() = musicLibrary.trackOverrides

    val userDataLoaded: StateFlow<Boolean> get() = container.userData.loaded

    val guiPrefs: StateFlow<GuiPrefs> get() = settings.guiPrefs

    val playerPrefs: StateFlow<PlayerPrefs> get() = settings.playerPrefs

    val audioFx: StateFlow<AudioFxState> get() = settings.audioFxState

    fun setGuiPrefs(prefs: GuiPrefs) = settings.setGuiPrefs(prefs)

    fun setPlayerPrefs(prefs: PlayerPrefs) = settings.setPlayerPrefs(prefs)

    fun setAudioFxEnabled(enabled: Boolean) = settings.setAudioFxEnabled(enabled)

    fun setAudioFxBand(
        band: Int,
        levelMb: Int,
    ) = settings.setAudioFxBand(band, levelMb)

    fun useAudioFxPreset(index: Int) = settings.useAudioFxPreset(index)

    fun setAudioFxBassBoost(strength: Int) = settings.setAudioFxBassBoost(strength)

    fun setAudioFxLoudness(gainMb: Int) = settings.setAudioFxLoudness(gainMb)

    fun toggleShuffle() = settings.toggleShuffle()

    fun cycleRepeatMode() = settings.cycleRepeatMode()

    private val textureController: TextureController =
        TextureController(
            application,
            scope,
            object : TextureController.Host {
                override fun onGeneratedPresetsRemoved(paths: List<String>) {
                    vizStateStore.dropRemovedMilkPaths(paths)
                }
            },
        )

    val textures: StateFlow<List<MilkTexture>> get() = textureController.textures

    private val modulation: ModulationController =
        ModulationController(
            LfoStore(prefsFiles.general),
            prefsFiles.modulation,
            object : ModulationController.Host {
                override val params: SceneParams get() = _vizState.value.params

                override val sceneId: String get() = _vizState.value.sceneId

                override fun setSceneParams(params: SceneParams) = visual.setSceneParams(params)
            },
        )

    val lfos: StateFlow<List<LfoConfig>> get() = modulation.lfos
    val adsrs: StateFlow<List<AdsrConfig>> get() = modulation.adsrs
    val reactions: StateFlow<List<dev.geode.render.ReactionRoute>?> get() = modulation.reactions
    val lockedParams: StateFlow<Set<String>> get() = modulation.lockedParams
    val paramHistory: StateFlow<ParamHistoryState> get() = modulation.history

    fun toggleParamLock(label: String) = modulation.toggleParamLock(label)

    fun randomizeParams(tab: CustomizeTab? = null) = modulation.randomizeParams(tab)

    fun editSceneParams(params: SceneParams) = modulation.editSceneParams(params)

    fun undoParams() = modulation.undo()

    fun redoParams() = modulation.redo()

    fun resetCustomizeTab(tab: CustomizeTab) = modulation.resetTab(tab)

    fun resetAllCustomize() = modulation.resetAll()

    fun setAdsr(
        index: Int,
        config: AdsrConfig,
    ) = modulation.setAdsr(index, config)

    fun setLfo(
        index: Int,
        config: LfoConfig,
    ) = modulation.setLfo(index, config)

    fun setReactions(routes: List<dev.geode.render.ReactionRoute>?) = modulation.setReactions(routes)

    fun rerollReactions() = modulation.rerollReactions()

    fun importTextures(
        uris: List<Uri>,
        onImported: () -> Unit,
    ) = textureController.importTextures(uris, onImported)

    fun removeTexture(name: String) = textureController.removeTexture(name)

    fun useTexture(
        name: String,
        onReady: (String) -> Unit,
    ) = textureController.useTexture(name, onReady)

    private val pcmScratch = FloatArray(4096)
    private var pcmCursor = 0L

    fun latestPcm(): PcmChunk? {
        val n = ring.copyNewSince(pcmCursor, pcmScratch)
        pcmCursor = ring.lastCopyEndIndex
        return if (n > 0) PcmChunk(pcmScratch, n) else null
    }

    private val milkImport = MilkImportController(application, scope)

    fun milkPresetFilesAsync(onDone: (List<MilkFile>) -> Unit) = milkImport.milkPresetFilesAsync(onDone)

    fun importMilkPresetAsync(
        uri: Uri,
        onDone: (String?) -> Unit,
    ) = milkImport.importMilkPresetAsync(uri, onDone)

    internal fun importMilkPresetBlocking(uri: Uri): String? = milkImport.importMilkPresetBlocking(uri)

    fun milkTextureLinksFor(path: String) = milkImport.textureLinksFor(path)

    fun assignMilkTextureAsync(
        path: String,
        expected: String,
        texture: String?,
        onDone: (List<dev.geode.data.MilkTextureLink>) -> Unit,
    ) = milkImport.assignTextureAsync(path, expected, texture, onDone)

    fun importMilkFolderAsync(
        treeUri: Uri,
        onDone: (MilkPackImporter.Report) -> Unit,
    ) = milkImport.importMilkFolderAsync(treeUri, onDone)

    private var currentUri: Uri? = null

    private val bookmarks =
        dev.geode.playback.TrackBookmarks(dev.geode.data.BookmarkStore(prefsFiles.player)) {
            settings.playerPrefs.value.resumeLongTracks
        }

    private val listening: ListeningTracker =
        ListeningTracker(
            application,
            favouritesRepository,
            sessionRepository,
            storeScope,
            object : ListeningTracker.Host {
                override val player: Player get() = this@PlayerSession.player
                override val currentUri: Uri? get() = this@PlayerSession.currentUri

                override fun mediaItemFor(uri: Uri) = queueController.mediaItemFor(uri)

                override fun mediaItemFor(track: QueueTrack) = queueController.mediaItemFor(track)
            },
        )

    val historyTick: StateFlow<Int> get() = listening.historyTick

    val favourites: StateFlow<Set<String>> get() = listening.favourites

    private val takeController: TakeController =
        TakeController(
            FileTakeRepository(TakeStore(application)),
            scope,
            storeScope,
            object : TakeController.Host {
                override val vizState: StateFlow<VizUiState> get() = _vizState
                override val activeMilkPath: String? get() = vizStateStore.activeMilkPath.value
                override val trackUri: String? get() = currentUri?.toString()

                override val trackPositionMs: Long
                    get() = runCatching { player.currentPosition }.getOrDefault(0L)

                override fun selectScene(sceneId: String) = visual.selectScene(sceneId)

                override fun setSceneParams(params: SceneParams) = visual.setSceneParams(params)

                override fun applyMilk(
                    path: String,
                    sceneId: String,
                ) = visual.emitApply(VizApply(milkPath = path, sceneId = sceneId))
            },
            takeName = { application.getString(dev.geode.R.string.take_default_name, it) },
        )

    val takeState: StateFlow<TakeUiState> get() = takeController.state

    private val _presetLocked = MutableStateFlow(false)
    val presetLocked: StateFlow<Boolean> = _presetLocked

    private val trackColor: TrackColorController =
        TrackColorController(
            application,
            scope,
            object : TrackColorController.Host {
                override val currentUri: Uri? get() = this@PlayerSession.currentUri
                override val params: SceneParams get() = _vizState.value.params

                override fun setSceneParams(params: SceneParams) = visual.setSceneParams(params)
            },
        )

    val artPaletteNote: StateFlow<String?> get() = trackColor.artPaletteNote

    fun applyArtworkPalette() = trackColor.applyArtworkPalette()

    private val _lyrics = MutableStateFlow<Lyrics?>(null)

    val lyrics: StateFlow<Lyrics?> = _lyrics

    private fun loadLyricsFor(uri: Uri?) {
        _lyrics.value = null
        if (uri == null) return
        scope.launch(Dispatchers.IO) {
            val found = LyricsLoader.load(application, uri)
            withContext(Dispatchers.Main) { if (currentUri == uri) _lyrics.value = found }
        }
    }

    private val _autoMode = MutableStateFlow(0)
    val autoMode: StateFlow<Int> = _autoMode

    private var playerListener: Player.Listener? = null

    private fun refresh() {
        _positionMs.value = player.currentPosition.coerceAtLeast(0)
        _uiState.value =
            PlayerUiState(
                isPlaying = player.isPlaying,
                durationMs = player.duration.coerceAtLeast(0),
                artist = player.mediaMetadata.artist?.toString(),
                title =
                    player.mediaMetadata.title?.toString()
                        ?: player.currentMediaItem
                            ?.localConfiguration
                            ?.uri
                            ?.lastPathSegment
                            ?.substringAfterLast('/')
                            ?.substringBeforeLast('.'),
                hasMedia = player.currentMediaItem != null,
                queueSize = player.mediaItemCount,
                queueIndex = player.currentMediaItemIndex,
                shuffle = player.shuffleModeEnabled,
                repeatMode = player.repeatMode,
            )
    }

    private val sleepTimer = playback.sleepTimer

    private val fades: PlaybackFades =
        PlaybackFades(
            scope,
            object : PlaybackFades.Host {
                override val player: Player get() = this@PlayerSession.player
                override val fadeMs: Int get() = settings.playerPrefs.value.fadeMs

                override fun stopLiveInput() {
                    if (captureController.micActive) setMicEnabled(false)
                }
            },
        )

    val sleepTimerRemainingMs: StateFlow<Long?> = sleepTimer.remainingMs

    fun startSleepTimer(minutes: Int) {
        if (minutes <= 0) {
            cancelSleepTimer()
            return
        }
        setPlayerPrefs(settings.playerPrefs.value.copy(sleepTimerMinutes = minutes))
        sleepTimer.start(minutes, settings.playerPrefs.value.sleepFinishTrack)
    }

    fun cancelSleepTimer() {
        sleepTimer.cancel()
    }

    val vizApply: SharedFlow<VizApply> get() = visual.vizApply

    val morphFade: SharedFlow<Float> get() = visual.morphFade

    private val autoVisuals: AutoVisualsController =
        AutoVisualsController(
            autoVisualsPrefsStore,
            object : AutoVisualsController.Host {
                override val vizState: StateFlow<VizUiState> get() = _vizState

                override fun updateViz(transform: (VizUiState) -> VizUiState) = _vizState.update(transform)

                override val isPlaying: Boolean get() = _uiState.value.isPlaying

                override val presetLocked: Boolean get() = _presetLocked.value

                // Auto-visuals only cycles through styles the user may keep; it never opens the
                // paywall on its own.
                override fun selectScene(sceneId: String) {
                    if (allowsStyle(sceneId)) visual.selectScene(sceneId)
                }

                override fun applyPreset(preset: Preset) {
                    if (allowsStyle(preset.sceneId)) visual.applyPreset(preset)
                }

                override fun applyMilk(
                    path: String,
                    sceneId: String,
                ) = visual.emitApply(VizApply(milkPath = path, sceneId = sceneId))

                override fun milkFilesAsync(onDone: (List<MilkFile>) -> Unit) = milkPresetFilesAsync(onDone)
            },
        )

    fun addToVizPlaylist(entry: VizPlaylistEntry) = autoVisuals.addToVizPlaylist(entry)

    fun removeVizPlaylistAt(index: Int) = autoVisuals.removeVizPlaylistAt(index)

    fun setVizPlaylistEnabled(enabled: Boolean) = autoVisuals.setVizPlaylistEnabled(enabled)

    fun setVizPlaylistInterval(seconds: Int) = autoVisuals.setVizPlaylistInterval(seconds)

    fun setRandomEnabled(enabled: Boolean) = autoVisuals.setRandomEnabled(enabled)

    fun setRandomInterval(seconds: Int) = autoVisuals.setRandomInterval(seconds)

    fun setRandomIncludeStyles(enabled: Boolean) = autoVisuals.setRandomIncludeStyles(enabled)

    fun setRandomIncludePresets(enabled: Boolean) = autoVisuals.setRandomIncludePresets(enabled)

    fun setRandomIncludeMilk(enabled: Boolean) = autoVisuals.setRandomIncludeMilk(enabled)

    fun setRandomizeColors(enabled: Boolean) = autoVisuals.setRandomizeColors(enabled)

    fun randomStepNow() = autoVisuals.randomStepNow()

    fun applyVizEntry(entry: VizPlaylistEntry) = autoVisuals.applyVizEntry(entry)

    val deviceTracks: StateFlow<List<DeviceTrack>> get() = musicLibrary.deviceTracks

    val mediaRoots: StateFlow<Set<String>> get() = musicLibrary.mediaRoots

    val libraryScanning: StateFlow<Boolean> get() = musicLibrary.libraryScanning

    fun refreshDeviceTracks() = musicLibrary.refreshDeviceTracks()

    fun importTracks(uris: List<Uri>) = musicLibrary.importTracks(uris)

    fun trackOverride(uri: String): LibraryTrack? = musicLibrary.trackOverride(uri)

    suspend fun trackInfoFor(uriStr: String): LibraryTrack = musicLibrary.trackInfoFor(uriStr)

    fun saveTrackInfo(
        uri: String,
        title: String,
        artist: String,
        album: String,
        genre: String,
        year: Int,
        trackNo: Int,
        comment: String,
    ) = musicLibrary.saveTrackInfo(uri, title, artist, album, genre, year, trackNo, comment)

    suspend fun writeTrackInfo(
        uri: String,
        title: String,
        artist: String,
        album: String,
        genre: String,
        year: Int,
        trackNo: Int,
        comment: String,
    ): TagWriteOutcome = musicLibrary.writeTrackInfo(uri, title, artist, album, genre, year, trackNo, comment)

    fun importFolder(treeUri: Uri) = musicLibrary.importFolder(treeUri)

    fun removeMediaRoot(uriStr: String) = musicLibrary.removeMediaRoot(uriStr)

    fun rescanMediaRoots() = musicLibrary.rescanMediaRoots()

    fun createMusicPlaylist(name: String) = musicLibrary.createMusicPlaylist(name)

    suspend fun importPlaylistFile(uri: Uri): PlaylistImportResult = musicLibrary.importPlaylistFile(uri)

    fun renameMusicPlaylist(
        oldName: String,
        newName: String,
    ): Boolean = musicLibrary.renameMusicPlaylist(oldName, newName)

    fun moveMusicPlaylistTrack(
        name: String,
        from: Int,
        to: Int,
    ) = musicLibrary.moveMusicPlaylistTrack(name, from, to)

    fun deleteMusicPlaylist(name: String) = musicLibrary.deleteMusicPlaylist(name)

    fun addTrackToPlaylist(
        playlist: String,
        uri: String,
    ) = musicLibrary.addTrackToPlaylist(playlist, uri)

    fun removeTrackFromPlaylist(
        playlist: String,
        uri: String,
    ) = musicLibrary.removeTrackFromPlaylist(playlist, uri)

    fun playPlaylist(
        playlist: String,
        startIndex: Int = 0,
    ) {
        val uris =
            library.value.playlists
                .firstOrNull { it.name == playlist }
                ?.trackUris
                .orEmpty()
        if (uris.isEmpty()) return
        val byUri = library.value.tracks.associateBy { it.uri }
        val tracks = uris.map { u -> byUri[u]?.let(PlaybackQueue::queueTrack) ?: QueueTrack(u) }
        playFrom(tracks, uris[startIndex.coerceIn(0, uris.size - 1)])
    }

    fun recentlyPlayed() = listening.recentlyPlayed()

    fun saveSmartPlaylist(playlist: SmartPlaylist) = musicLibrary.saveSmartPlaylist(playlist)

    fun deleteSmartPlaylist(name: String) = musicLibrary.deleteSmartPlaylist(name)

    fun resolveSmartPlaylist(playlist: SmartPlaylist): List<DeviceTrack> =
        SmartPlaylistMatcher.resolve(playlist, deviceTracks.value, listening::playCountOf, favourites.value)

    fun playSmartPlaylist(playlist: SmartPlaylist) {
        val tracks = resolveSmartPlaylist(playlist).map(PlaybackQueue::queueTrack)
        if (tracks.isNotEmpty()) playFrom(tracks, tracks.first().uri)
    }

    fun currentTrackUri(): String? = currentUri?.toString()

    fun toggleFavourite(uri: String? = null) {
        listening.toggleFavourite(uri ?: currentUri?.toString() ?: return)
    }

    val abLoop: StateFlow<AbLoop?> get() = queueController.abLoop

    fun cycleAbLoop() = queueController.cycleAbLoop()

    fun clearAbLoop() = queueController.clearAbLoop()

    val queue: StateFlow<QueueUiState> get() = queueController.queue

    fun removeQueueItem(index: Int) = queueController.removeQueueItem(index)

    fun moveQueueItem(
        from: Int,
        to: Int,
    ) = queueController.moveQueueItem(from, to)

    fun togglePlayPauseFaded() = fades.togglePlayPauseFaded()

    fun resumeLastPlayed() {
        if (player.currentMediaItem != null) {
            player.play()
            return
        }
        val last = listening.recentlyPlayed(1).firstOrNull() ?: return
        playTrack(last.uri)
    }

    fun togglePresetLock() {
        _presetLocked.update { !it }
    }

    fun cycleAutoMode() {
        val next = (_autoMode.value + 1) % 2
        _autoMode.value = next
        setRandomEnabled(next == 1)
    }

    fun playNext(uri: String) = queueController.playNext(uri)

    fun enqueue(uri: String) = queueController.enqueue(uri)

    fun shuffleAllHistory() {
        playAll(listening.recentlyPlayed(100).map { QueueTrack(it.uri, it.title) }, shuffled = true)
    }

    val presetFolders: StateFlow<PresetFolders> get() = presetLibrary.folders

    fun addPresetFolder(path: String) = presetLibrary.addPresetFolder(path)

    fun renamePresetFolder(
        from: String,
        to: String,
    ) = presetLibrary.renamePresetFolder(from, to)

    fun movePresetToFolder(
        name: String,
        folder: String,
    ) = presetLibrary.movePresetToFolder(name, folder)

    fun userMilkPresets(): List<File> = presetLibrary.userMilkPresets()

    private val queueController: QueueController =
        QueueController(
            application,
            object : QueueController.Host {
                override val player: Player get() = this@PlayerSession.player
                override val libraryTracks: List<LibraryTrack> get() = library.value.tracks
                override val deviceTracks: List<DeviceTrack> get() = musicLibrary.deviceTracks.value

                override fun stopLiveInput() {
                    if (captureController.micActive) setMicEnabled(false)
                }

                override fun onQueueStarted(startUri: Uri) {
                    currentUri = startUri
                    onTrackChanged()
                }

                override fun skipFaded(action: () -> Unit) = fades.skipFaded(action)

                override fun refreshUi() = refresh()
            },
        )

    fun playTrack(uri: String) = queueController.playTrack(uri)

    /** Voice search: the library matches for [query] as a queue, or everything when the query is blank. */
    fun playFromSearch(query: String) {
        scope.launch {
            val all = withContext(Dispatchers.IO) { DeviceTrackQuery.query(application) }
            val matches = LibraryBrowse.search(all, query)
            val queue = matches.map(PlaybackQueue::queueTrack)
            if (queue.isNotEmpty()) playFrom(queue, queue.first().uri)
        }
    }

    fun playFrom(
        tracks: List<QueueTrack>,
        startUri: String,
    ) = queueController.playFrom(tracks, startUri)

    fun playAll(
        tracks: List<QueueTrack>,
        shuffled: Boolean = false,
    ) = queueController.playAll(tracks, shuffled)

    fun open(uris: List<Uri>) = queueController.open(uris)

    fun queueTitles(): List<String> = queueController.queueTitles()

    fun playQueueIndex(index: Int) = queueController.playQueueIndex(index)

    fun next() = queueController.next()

    fun previous() = queueController.previous()

    fun togglePlayPause() {
        if (!player.isPlaying && captureController.micActive) setMicEnabled(false)
        if (settings.playerPrefs.value.fadeMs > 0) {
            togglePlayPauseFaded()
        } else if (player.isPlaying) {
            player.pause()
        } else {
            // play() alone is a no-op once the track has ENDED (repeat off); this rewinds first.
            Util.handlePlayButtonAction(player)
        }
    }

    fun seekToMs(positionMs: Long) = queueController.seekToMs(positionMs)

    fun seekTo(fraction: Float) = queueController.seekTo(fraction)

    private fun onTrackChanged() {
        clearAbLoop()
        loadLyricsFor(currentUri)
    }

    private val _playbackNotice = MutableStateFlow<String?>(null)

    val playbackNotice: StateFlow<String?> = _playbackNotice

    private var consecutivePlaybackFailures = 0

    fun clearPlaybackNotice() {
        _playbackNotice.value = null
    }

    fun startRecording() = takeController.startRecording()

    fun stopRecording(name: String? = null) = takeController.stopRecording(name)

    fun playTake(name: String) = takeController.playTake(name)

    fun stopReplay() = takeController.stopReplay()

    fun deleteTake(name: String) = takeController.deleteTake(name)

    fun renameTake(
        from: String,
        to: String,
    ) = takeController.renameTake(from, to)

    fun setExportTake(name: String?) = takeController.setExportTake(name)

    /**
     * The Pro style gate, set by [dev.geode.di.PlayerSessionProvider] from the entitlement use
     * case. Open until set, so a session built outside the provider (a test) behaves as before.
     */
    internal var allowsStyle: (String) -> Boolean = { true }

    /** What a refused style does: the provider opens the paywall. */
    internal var onStyleLocked: () -> Unit = {}

    fun selectScene(sceneId: String) = if (allowsStyle(sceneId)) visual.selectScene(sceneId) else onStyleLocked()

    fun setReactivity(
        attack: Float,
        decay: Float,
    ) = visual.setReactivity(attack, decay)

    fun setSceneParams(params: SceneParams) = visual.setSceneParams(params)

    fun resetSceneParams() = visual.resetSceneParams()

    fun nudgeTransform(
        zoomFactor: Float,
        rotationDegrees: Float,
    ) = visual.nudgeTransform(zoomFactor, rotationDegrees)

    fun reportShaderError(error: String?) = visual.reportShaderError(error)

    fun applyCustomShader(source: String) = visual.applyCustomShader(source)

    fun setTransitionStyle(style: TransitionStyle) = visual.setTransitionStyle(style)

    fun setTransitionId(id: String) = visual.setTransitionId(id)

    fun setTransitionDuration(seconds: Float) = visual.setTransitionDuration(seconds)

    fun applyPreset(preset: Preset) = if (allowsStyle(preset.sceneId)) visual.applyPreset(preset) else onStyleLocked()

    fun savePreset(
        name: String,
        customShader: String?,
        folder: String = "",
    ) = presetLibrary.savePreset(name, customShader, folder)

    internal fun milkPresetPathFor(preset: Preset): String? = presetLibrary.milkPresetPathFor(preset)

    val activeMilkPath: StateFlow<String?> get() = vizStateStore.activeMilkPath

    fun noteMilkPreset(path: String) = vizStateStore.noteMilkPreset(path)

    fun presetShareLink(name: String): String? = presetLibrary.presetShareLink(name)

    fun importPresetLink(text: String): String? = presetLibrary.importPresetLink(text)

    fun importPresetFile(
        uri: Uri,
        onResult: (String?) -> Unit,
    ) = presetLibrary.importPresetFile(uri, onResult)

    fun presetFile(name: String): File? = presetLibrary.presetFile(name)

    fun deletePreset(name: String) = presetLibrary.deletePreset(name)

    val templates: StateFlow<List<VideoTemplate>> get() = templateLibrary.library

    val templateStarters: List<VideoTemplate> get() = templateLibrary.starters

    fun applyTemplate(template: VideoTemplate) = templateLibrary.applyTemplate(template)

    fun saveCurrentAsTemplate(
        name: String,
        customShader: String?,
        onResult: (TemplateWrite) -> Unit,
    ) = templateLibrary.saveCurrentAsTemplate(name, customShader, onResult)

    fun adoptTemplate(
        starter: VideoTemplate,
        onResult: (TemplateImport) -> Unit,
    ) = templateLibrary.adopt(starter, onResult)

    fun deleteTemplate(id: TemplateId) = templateLibrary.deleteTemplate(id)

    fun importTemplateText(
        text: String,
        onResult: (TemplateImport) -> Unit,
    ) = templateLibrary.importTemplateText(text, onResult)

    fun importTemplateFile(
        uri: Uri,
        onResult: (TemplateImport) -> Unit,
    ) = templateLibrary.importTemplateFile(uri, onResult)

    fun templateShareLink(template: VideoTemplate): String? = templateLibrary.shareLink(template)

    private val exportController: ExportController =
        ExportController(
            application,
            scope,
            object : ExportController.Host {
                override val exportUri: Uri? get() = currentUri
                override val guiPrefs: GuiPrefs get() = settings.guiPrefs.value
                override val sceneId: String get() = _vizState.value.sceneId
                override val sceneParams get() = _vizState.value.params

                override fun lfoConfigs() = modulation.lfos.value

                override fun adsrConfigs() = modulation.adsrs.value

                override fun reactions() = modulation.reactions.value

                override fun keyframes() = editor.state.value.project.keyframes

                override suspend fun loadExportTake() = takeController.loadExportTake()
            },
        )

    val studio: StateFlow<StudioUiState> get() = exportController.studio

    internal val editor: EditorController = EditorController(EditorProjectStore(application), scope, storeScope).also { it.open() }

    // W02: the background image behind the scene - see BackgroundController for why it takes no Host.
    private val backgroundController: BackgroundController =
        BackgroundController(application, storeScope, BackgroundPrefsStore(prefsFiles.background)).also { it.start() }

    val backgroundPrefs: StateFlow<BackgroundPrefs> get() = backgroundController.prefs
    val backgroundPush: StateFlow<BackgroundPushState> get() = backgroundController.push

    fun pickBackgroundImage(uri: Uri) = backgroundController.pick(uri)

    fun clearBackgroundImage() = backgroundController.clear()

    fun setBackgroundBlend(blend: UnderlayBlend) = backgroundController.setBlend(blend)

    fun setBackgroundAmount(amount: Float) = backgroundController.setAmount(amount)

    fun setBackgroundBlurRadius(radius: Int) = backgroundController.setBlurRadius(radius)

    fun setBackgroundDim(dim: Float) = backgroundController.setDim(dim)

    fun setBackgroundRenderSize(
        width: Int,
        height: Int,
    ) = backgroundController.setRenderSize(width, height)

    fun currentSceneId(): String = _vizState.value.sceneId

    fun currentSceneParams(): SceneParams = _vizState.value.params

    fun playbackPositionMs(): Long? = if (player.isPlaying) player.currentPosition else null

    fun trackDurationMs(): Long = player.duration.coerceAtLeast(0L)

    val playbackRepository: PlaybackRepository = SessionPlaybackRepository(this)

    val visualizerRepository: VisualizerRepository = SessionVisualizerRepository(this)

    val userDataRepository: UserDataRepository get() = container.userData

    fun startExport(
        aspect: ExportAspect,
        fps: Int,
        sceneFactory: SceneFactory,
        destination: Uri? = null,
        range: ExportRange? = null,
        sceneFactoryFor: ((String) -> SceneFactory)? = null,
        codec: ExportCodec = ExportCodec.H264,
        watermark: Boolean = false,
    ) = exportController.startExport(aspect, fps, sceneFactory, destination, range, sceneFactoryFor, codec, watermark)

    fun startBatch(
        tracks: List<BatchTrack>,
        aspect: ExportAspect,
        fps: Int,
        sceneFactory: SceneFactory,
        sceneParams: SceneParams?,
        codec: ExportCodec,
        watermark: Boolean,
    ) = exportController.startBatch(tracks, aspect, fps, sceneFactory, sceneParams, codec, watermark)

    fun cancelExport() = exportController.cancelExport()

    fun resetExportState() = exportController.resetExportState()

    fun refreshStudioClips() = exportController.refreshStudioClips()

    fun deleteStudioClip(
        uri: String,
        onResult: (Boolean) -> Unit,
    ) = exportController.deleteStudioClip(uri, onResult)

    fun renameStudioClip(
        uri: String,
        name: String,
        onResult: (Boolean) -> Unit,
    ) = exportController.renameStudioClip(uri, name, onResult)

    fun describeStudioClip(
        uri: Uri,
        onReady: (StudioClip) -> Unit,
    ) = exportController.describeStudioClip(uri, onReady)

    fun startStudioExport(
        clip: StudioClip,
        edit: dev.geode.export.ClipEdit,
        destination: Uri? = null,
    ) = exportController.startStudioExport(clip, edit, destination)

    fun cancelStudioExport() = exportController.cancelStudioExport()

    fun startProjectExport(destination: Uri? = null) = exportController.startProjectExport(editor.state.value.project, destination)

    fun clearStudioResult() = exportController.clearStudioResult()

    val loopState: StateFlow<LoopUiState> get() = exportController.loopState

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
        watermark: Boolean = false,
    ) = exportController.startLoopRender(aspect, codec, fps, sceneFactory, loopMs, crossfadeMs, drift, audioClips, destination, watermark)

    fun cancelLoopRender() = exportController.cancelLoopRender()

    fun clearLoopResult() = exportController.clearLoopResult()

    /**
     * Tears the session down. Called only by [dev.geode.di.PlayerSessionProvider], on behalf of the
     * one ViewModel that owns the session — never from a screen.
     */
    internal fun shutdown() {
        // Queue every outstanding write BEFORE the pending set is snapshotted below, or the store
        // scope's cancellation races the very writes this teardown exists to preserve.
        listening.flushListenTime()
        vizStateStore.flushIfDirty()
        val pendingStoreWrites =
            storeScope.coroutineContext.job.children
                .toList()

        captureController.shutdown()
        playerListener?.let { player.removeListener(it) }
        queueController.release()
        playerListener = null
        if (playback.onAudioFormat === captureController.audioFormatHook) playback.onAudioFormat = null
        if (sleepTimer.onFadeVolume === fades.sleepFadeHook) sleepTimer.onFadeVolume = null
        if (!playback.playbackWanted) PlaybackService.stop(application)
        PlaybackEngine.releaseUi()
        scope.cancel()

        drainStoreWrites(pendingStoreWrites)
    }

    /**
     * Lets the queued store writes finish, then retires the scope that owns them.
     *
     * This runs on the app scope's IO dispatcher rather than the caller's thread. `shutdown()` is
     * reached from `PlayerViewModel.onCleared()`, which is the main thread: the previous version
     * spent its whole [STORE_FLUSH_BUDGET_MS] budget there, between a blocking `Future.get` and a
     * `runBlocking`, so a slow disk stalled Activity teardown for up to two seconds.
     *
     * The budget and the ordering are unchanged; only the thread that waits them out is different.
     * The trade is that durability past this point is best-effort — the process could in principle
     * be killed mid-drain — but the writes are already queued on a serialised IO dispatcher, and
     * blocking the main thread bought only the window between destroy and death.
     */
    private fun drainStoreWrites(pending: List<Job>) {
        container.appScope.launch(Dispatchers.IO) {
            withTimeoutOrNull(STORE_FLUSH_BUDGET_MS) {
                listening.awaitHistoryWrites(STORE_FLUSH_BUDGET_MS)
                pending.joinAll()
            }
            storeScope.cancel()
        }
    }

    init {
        // Last init block in the class, so every field above is assigned by now — which is what
        // the capture controller needs before it may call back into this session.
        captureController.start()
        musicLibrary.refreshNumericTitles()
        takeController.refresh()
        presetLibrary.refreshInitial()
        templateLibrary.refreshInitial()
        musicLibrary.refresh()
        textureController.refresh()
        scope.launch {
            attachPlayback()
            pollPlaybackState()
        }
    }

    private suspend fun attachPlayback() {
        val pp = settings.loadedPlayerPrefs()
        player.shuffleModeEnabled = pp.shuffle
        player.repeatMode = pp.repeatMode
        settings.applyPlaybackPrefs(pp)
        val alreadyLoaded = player.currentMediaItem != null
        if (alreadyLoaded) {
            currentUri = player.currentMediaItem?.localConfiguration?.uri
        } else if (pp.autoResume) {
            listening.prepareLastPlayed()?.let { currentUri = it }
        }
        val listener =
            object : Player.Listener {
                override fun onEvents(
                    player: Player,
                    events: Player.Events,
                ) {
                    refresh()
                    if (events.contains(Player.EVENT_MEDIA_ITEM_TRANSITION)) {
                        currentUri = player.currentMediaItem?.localConfiguration?.uri
                        currentUri?.let { u ->
                            val title =
                                player.mediaMetadata.title?.toString()
                                    ?: player.currentMediaItem
                                        ?.localConfiguration
                                        ?.uri
                                        ?.lastPathSegment
                                        .orEmpty()
                            listening.recordPlay(
                                u.toString(),
                                title,
                                player.mediaMetadata.artist
                                    ?.toString()
                                    .orEmpty(),
                            )
                        }
                        onTrackChanged()
                        bookmarks.onTrackStarted(player)
                    }
                }

                override fun onAudioSessionIdChanged(audioSessionId: Int) {
                    audioFxController.attach(audioSessionId)
                    settings.refreshAudioFx()
                }

                override fun onPlayerError(error: PlaybackException) {
                    consecutivePlaybackFailures++
                    val failed =
                        player.currentMediaItem
                            ?.mediaMetadata
                            ?.title
                            ?.toString()
                    val action =
                        PlaybackErrors.decide(
                            consecutivePlaybackFailures,
                            hasNext = player.hasNextMediaItem(),
                        )
                    _playbackNotice.value = PlaybackErrors.describe(error.errorCode, failed, action)
                    when (action) {
                        PlaybackErrors.Action.SkipToNext -> {
                            player.seekToNextMediaItem()
                            player.prepare()
                        }

                        PlaybackErrors.Action.StopEndOfQueue,
                        PlaybackErrors.Action.StopSourceUnavailable,
                        -> player.pause()
                    }
                }

                override fun onIsPlayingChanged(isPlaying: Boolean) {
                    if (!isPlaying) return
                    consecutivePlaybackFailures = 0
                    if (captureController.micActive) setMicEnabled(false)
                    if (externalAudio.value.active) stopExternalAudio()
                    fades.ensureAudibleAfterExternalPlay()
                    PlaybackService.ensureRunning(application)
                }
            }
        playerListener = listener
        player.addListener(listener)
        sleepTimer.onFadeVolume = fades.sleepFadeHook
        playback.exoPlayer?.let { audioFxController.attach(it.audioSessionId) }
        settings.refreshAudioFx()
        if (alreadyLoaded) onTrackChanged()
    }

    private suspend fun pollPlaybackState(): Nothing {
        while (true) {
            refresh()
            listening.accrueListenTime()
            queueController.enforceAbLoop()
            queueController.refreshQueue()
            captureController.refreshExternalAudio()
            captureController.refreshMicState()
            autoVisuals.advanceVizPlaylist()
            autoVisuals.advanceRandomMode()
            listening.persistSession()
            bookmarks.onPoll(player)
            delay(POLL_INTERVAL_MS)
        }
    }

    private companion object {
        const val STORE_FLUSH_BUDGET_MS = 2_000L
        const val POLL_INTERVAL_MS = 500L
    }
}
