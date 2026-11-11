package dev.geode.ui

import android.app.Application
import android.net.Uri
import androidx.annotation.OptIn
import androidx.media3.common.PlaybackException
import androidx.media3.common.Player
import androidx.media3.common.util.UnstableApi
import androidx.media3.common.util.Util
import dev.geode.audio.MicCapture
import dev.geode.data.AutoVisualsPrefsStore
import dev.geode.data.BackgroundPrefsStore
import dev.geode.data.BehaviourStore
import dev.geode.data.DeviceTrackQuery
import dev.geode.data.EditorProjectStore
import dev.geode.data.FavouritesRepository
import dev.geode.data.FilePresetRepository
import dev.geode.data.FileSessionRepository
import dev.geode.data.FileTemplateRepository
import dev.geode.data.GuiPrefs
import dev.geode.data.LfoStore
import dev.geode.data.LibraryState
import dev.geode.data.Lyrics
import dev.geode.data.LyricsLoader
import dev.geode.data.MilkPackImporter
import dev.geode.data.MusicLibraryRepository
import dev.geode.data.PlayerPrefs
import dev.geode.data.PlayerPrefsCodec
import dev.geode.data.PlayerPrefsRepository
import dev.geode.data.Preset
import dev.geode.data.PresetRepository
import dev.geode.data.PresetStore
import dev.geode.data.SessionRepository
import dev.geode.data.SessionStore
import dev.geode.data.SharedPrefsFavouritesRepository
import dev.geode.data.SmartPlaylist
import dev.geode.data.SmartPlaylistMatcher
import dev.geode.data.SystemMotion
import dev.geode.data.TemplateImport
import dev.geode.data.TemplateLook
import dev.geode.data.TemplateRepository
import dev.geode.data.TemplateWrite
import dev.geode.data.UserDataRepository
import dev.geode.data.VideoTemplate
import dev.geode.data.VizStateStore
import dev.geode.data.VizUiState
import dev.geode.data.prefs.PrefsSettingsRepository
import dev.geode.engine.bridge.GeodeNative
import dev.geode.export.ExportAspect
import dev.geode.export.ExportCodec
import dev.geode.export.ExportRange
import dev.geode.geodeContainer
import dev.geode.model.DeviceTrack
import dev.geode.model.LibraryTrack
import dev.geode.model.MilkFile
import dev.geode.model.QueueTrack
import dev.geode.model.QueueUiState
import dev.geode.playback.PlaybackEngine
import dev.geode.playback.PlaybackErrors
import dev.geode.playback.PlaybackQueue
import dev.geode.playback.PlaybackService
import dev.geode.render.AdsrConfig
import dev.geode.render.MilkPresetMemory
import dev.geode.render.SceneFactory
import dev.geode.render.look.LookSource
import dev.geode.render.scene.Catalogue
import dev.geode.render.scene.MilkdropEngine
import dev.geode.render.scene.SceneIds
import dev.geode.render.scene.SceneParams
import dev.geode.util.bestEffort
import dev.geode.widget.WidgetStyles
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.drop
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.job
import kotlinx.coroutines.joinAll
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeoutOrNull
import kotlin.random.Random

@OptIn(UnstableApi::class)
class PlayerSession internal constructor(
    private val application: Application,
) {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)

    private val playback = PlaybackEngine.acquireForUi(application)

    @OptIn(ExperimentalCoroutinesApi::class)
    private val storeScope = CoroutineScope(SupervisorJob() + Dispatchers.IO.limitedParallelism(1))

    private val container = application.geodeContainer

    private val prefsFiles = container.prefsFiles

    private val playerPrefsRepository: PlayerPrefsRepository =
        PrefsSettingsRepository(prefsFiles.player, PlayerPrefsCodec, storeScope)

    private val favouritesRepository: FavouritesRepository =
        SharedPrefsFavouritesRepository(prefsFiles.favourites, storeScope)

    private val sessionRepository: SessionRepository = FileSessionRepository(SessionStore(application))

    private val presetRepository: PresetRepository = FilePresetRepository(PresetStore(application))

    internal val captureController: CaptureController =
        CaptureController(
            application,
            scope,
            playback.sampleRing,
            playback.micRing,
            object : CaptureController.Host {
                override fun pausePlayback() = player.pause()

                override fun resetSound(playerOwnsRing: Boolean) {
                    // The tap first: once this returns it writes nothing while another source has the ring.
                    playback.tap.owns = playerOwnsRing
                    playback.sampleRing.beginEpoch()
                    bestEffort("PlayerSession", "player source") { GeodeNative.soundSetPlayerSource(playerOwnsRing) }
                }

                override fun setMicReactivePref(on: Boolean) {
                    setGuiPrefs(guiPrefs.value.copy(micReactive = on))
                }

                override val micReactivePref: Boolean get() = guiPrefs.value.micReactive
            },
        )

    val externalAudio: StateFlow<ExternalAudioState> get() = captureController.externalAudio

    fun setMicEnabled(enabled: Boolean): MicCapture.Failure? = captureController.setMicEnabled(enabled)

    fun stopExternalAudio() = captureController.stopExternalAudio()

    internal val presetLibrary: PresetLibraryController =
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

    internal val templateLibrary: TemplateController =
        TemplateController(
            application,
            templateRepository,
            scope,
            storeScope,
            object : TemplateController.Host {
                override val vizState: StateFlow<VizUiState> get() = _vizState
                override val activeMilkPath: String? get() = vizStateStore.activeMilkPath.value

                // Through the session's own applyPreset, not straight to the visual controller: that
                // is where the Pro style gate sits, so a template on a locked style is refused and
                // opens the paywall exactly as the same look saved as a preset would (PRO-1).
                override fun applyLook(
                    look: TemplateLook,
                    name: String,
                ) = applyPreset(look.toPreset(name))
            },
        )

    private val autoVisualsPrefsStore = AutoVisualsPrefsStore(prefsFiles.viz)
    internal val vizStateStore = VizStateStore(prefsFiles.viz, scope, autoVisualsPrefsStore, MilkPresetMemory.shared(application))
    private val audioFxController = playback.audioFx

    internal val settings: PlayerSettingsController =
        PlayerSettingsController(
            container.userData,
            playerPrefsRepository,
            container.appScope,
            playback.player,
            playback.audioFx,
            object : PlayerSettingsController.Host {
                override fun refreshUi() = refresh()

                override fun applyPlaybackPrefs(prefs: PlayerPrefs) = playback.applyPrefs(prefs)
            },
        )

    internal val visual: VisualSettingsController =
        VisualSettingsController(
            vizStateStore,
            scope,
            object : VisualSettingsController.Host {
                override val guiPrefs: GuiPrefs get() = settings.guiPrefs.value

                override suspend fun milkPathFor(preset: Preset): String? = presetLibrary.milkPresetPathFor(preset)
            },
        )

    val player: Player = playback.player

    private val _uiState = MutableStateFlow(PlayerUiState())
    val uiState: StateFlow<PlayerUiState> = _uiState

    private val _positionMs = MutableStateFlow(0L)
    val positionMs: StateFlow<Long> = _positionMs

    private val _vizState get() = vizStateStore.state
    val vizState: StateFlow<VizUiState> get() = vizStateStore.state

    /** The music library: tracks, folders, playlists and tags. [LibraryViewModel] calls it directly. */
    internal val musicLibrary = MusicLibraryRepository(application, prefsFiles.library, scope)

    val library: StateFlow<LibraryState> get() = musicLibrary.library

    val userDataLoaded: StateFlow<Boolean> get() = container.userData.loaded

    val guiPrefs: StateFlow<GuiPrefs> get() = settings.guiPrefs

    fun setGuiPrefs(prefs: GuiPrefs) = settings.setGuiPrefs(prefs)

    fun setPlayerPrefs(prefs: PlayerPrefs) = settings.setPlayerPrefs(prefs)

    internal val textureController: TextureController =
        TextureController(
            application,
            scope,
            object : TextureController.Host {
                override fun onGeneratedPresetsRemoved(paths: List<String>) {
                    vizStateStore.dropRemovedMilkPaths(paths)
                }
            },
        )

    internal val modulation: ModulationController =
        ModulationController(
            LfoStore(prefsFiles.general),
            prefsFiles.modulation,
            object : ModulationController.Host {
                override val params: SceneParams get() = _vizState.value.params

                override val sceneId: String get() = _vizState.value.sceneId

                override fun setSceneParams(params: SceneParams) = visual.setSceneParams(params)
            },
        )

    val adsrs: StateFlow<List<AdsrConfig>> get() = modulation.adsrs
    val handMask: StateFlow<Int> get() = modulation.handMask
    val paramHistory: StateFlow<ParamHistoryState> get() = modulation.history

    fun importTextures(
        uris: List<Uri>,
        onImported: () -> Unit,
    ) = textureController.importTextures(uris, onImported)

    fun useTexture(
        name: String,
        onReady: (String) -> Unit,
    ) = textureController.useTexture(name, onReady)

    internal val milkImport = MilkImportController(application, scope)

    fun milkPresetFilesAsync(onDone: (List<MilkFile>) -> Unit) = milkImport.milkPresetFilesAsync(onDone)

    fun importMilkPresetAsync(
        uri: Uri,
        onDone: (String?) -> Unit,
    ) = milkImport.importMilkPresetAsync(uri, onDone)

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

    internal val listening: ListeningTracker =
        ListeningTracker(
            playback.history,
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

    val favourites: StateFlow<Set<String>> get() = listening.favourites

    private val recordPrefs = dev.geode.data.RecordPrefsStore(prefsFiles.general)
    private val exportPrefs = dev.geode.data.ExportPrefsStore(prefsFiles.general)

    internal val record: RecordController =
        RecordController(
            application,
            scope,
            dev.geode.data.RecordFiles(application),
            object : RecordController.Host {
                override fun canRecord(): Boolean = allowsRecording()

                override fun onRecordLocked() = onRecordingLocked()

                override fun prefs() = recordPrefs.load(exportPrefs.loadFor(dev.geode.data.ExportDestination.CUSTOM))

                override fun limits() = renderLimits()

                override fun track(): dev.geode.editor.TrackRef? =
                    currentUri?.let { uri ->
                        dev.geode.editor.TrackRef(
                            uri.toString(),
                            player.mediaMetadata.title
                                ?.toString()
                                .orEmpty(),
                            trackDurationMs(),
                        )
                    }

                override fun keep(
                    track: dev.geode.editor.TrackRef?,
                    render: dev.geode.editor.ProjectRender,
                ) = editor.keepRecording(track, render)
            },
        )

    internal val trackColor: TrackColorController =
        TrackColorController(
            application,
            scope,
            object : TrackColorController.Host {
                override val currentUri: Uri? get() = this@PlayerSession.currentUri
                override val params: SceneParams get() = _vizState.value.params

                override fun setSceneParams(params: SceneParams) = visual.setSceneParams(params)
            },
        )

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

    /** Keeps the .lrc at [source] as the playing track's lyrics (Settings › Lyrics) and shows it. */
    fun attachLyrics(source: Uri) {
        val uri = currentUri ?: return
        scope.launch(Dispatchers.IO) {
            if (LyricsLoader.attach(application, uri, source)) withContext(Dispatchers.Main) { loadLyricsFor(uri) }
        }
    }

    private var playerListener: Player.Listener? = null

    private fun refresh() {
        val uri = player.currentMediaItem?.localConfiguration?.uri
        _positionMs.value = player.currentPosition.coerceAtLeast(0)
        _uiState.value =
            PlayerUiState(
                isPlaying = player.isPlaying,
                durationMs = player.duration.coerceAtLeast(0),
                artist = player.mediaMetadata.artist?.toString(),
                title =
                    player.mediaMetadata.title?.toString()
                        ?: uri
                            ?.lastPathSegment
                            ?.substringAfterLast('/')
                            ?.substringBeforeLast('.'),
                hasMedia = player.currentMediaItem != null,
                trackUri = uri?.toString(),
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

                override fun rampTo(
                    audible: Boolean,
                    ms: Int,
                ) = playback.rampUiFade(audible, ms)
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

    internal val autoVisuals: AutoVisualsController =
        AutoVisualsController(
            autoVisualsPrefsStore,
            object : AutoVisualsController.Host {
                override val vizState: StateFlow<VizUiState> get() = _vizState

                override fun updateViz(transform: (VizUiState) -> VizUiState) = _vizState.update(transform)

                override val isPlaying: Boolean get() = _uiState.value.isPlaying

                // Auto-visuals only cycles through styles the user may keep; it never opens the
                // paywall on its own.
                override fun selectScene(sceneId: String) {
                    if (allowsStyle(sceneId)) visual.selectScene(sceneId)
                }

                override fun allowsStyle(sceneId: String) = this@PlayerSession.allowsStyle(sceneId)

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

    val deviceTracks: StateFlow<List<DeviceTrack>> get() = musicLibrary.deviceTracks

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

    /** The favourites still on the device, newest first: Library's Favourites list. */
    fun favouriteTracks(): List<DeviceTrack> = LibraryBrowse.favouriteTracks(favourites.value, deviceTracks.value)

    fun playFavourites() {
        val tracks = favouriteTracks().map(PlaybackQueue::queueTrack)
        if (tracks.isNotEmpty()) playFrom(tracks, tracks.first().uri)
    }

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

    fun clearAbLoop() = queueController.clearAbLoop()

    val queue: StateFlow<QueueUiState> get() = queueController.queue

    fun togglePlayPauseFaded() = fades.togglePlayPauseFaded()

    fun resumeLastPlayed() {
        if (player.currentMediaItem != null) {
            player.play()
            return
        }
        val last = listening.recentlyPlayed(1).firstOrNull() ?: return
        playTrack(last.uri)
    }

    fun shuffleAllHistory() {
        playAll(listening.recentlyPlayed(100).map { QueueTrack(it.uri, it.title) }, shuffled = true)
    }

    internal val queueController: QueueController =
        QueueController(
            application,
            object : QueueController.Host {
                override val player: Player get() = this@PlayerSession.player
                override val libraryTracks: List<LibraryTrack> get() = library.value.tracks
                override val deviceTracks: List<DeviceTrack> get() = musicLibrary.deviceTracks.value

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

    fun togglePlayPause() {
        if (settings.playerPrefs.value.fadeMs > 0) {
            togglePlayPauseFaded()
        } else if (player.isPlaying) {
            player.pause()
        } else {
            // play() alone is a no-op once the track has ENDED (repeat off); this rewinds first.
            Util.handlePlayButtonAction(player)
        }
    }

    private fun onTrackChanged() {
        clearAbLoop()
        loadLyricsFor(currentUri)
    }

    private val _playbackNotice = MutableStateFlow<String?>(null)

    val playbackNotice: StateFlow<String?> = _playbackNotice

    fun clearPlaybackNotice() {
        _playbackNotice.value = null
    }

    /**
     * The Pro style gate, set by [dev.geode.di.PlayerSessionProvider] from the entitlement use
     * case. Open until set, so a session built outside the provider (a test) behaves as before.
     */
    internal var allowsStyle: (String) -> Boolean = { true }

    /** What a refused style does: the provider opens the paywall. */
    internal var onStyleLocked: () -> Unit = {}

    /**
     * The entitlement's export limits for every project render (a take, Studio's Render), set by the
     * provider like [allowsStyle]; open until set.
     */
    internal var renderLimits: () -> dev.geode.pro.RenderLimits = { dev.geode.pro.RenderLimits.NONE }

    /** The Pro gate for Record (part 2D), set by the provider like [allowsStyle]; open until set. */
    internal var allowsRecording: () -> Boolean = { true }

    /** What a refused recording does: the provider opens the paywall. */
    internal var onRecordingLocked: () -> Unit = {}

    fun selectScene(sceneId: String) {
        pendingUnlockSceneId = null
        if (allowsStyle(sceneId)) {
            visual.selectScene(sceneId)
        } else {
            // Kept so that buying Pro from this paywall ends on the style that asked for it.
            pendingUnlockSceneId = sceneId
            onStyleLocked()
        }
    }

    /**
     * The locked style whose choice opened the paywall, if any. The provider chooses it once the
     * entitlement lets it through: someone who pressed "Unlock with Pro" on a previewed style and
     * paid wants that style, not a second trip to the grid. Any other choice clears it.
     */
    @Volatile
    internal var pendingUnlockSceneId: String? = null

    /**
     * Previews are not gated: seeing a Pro style move to your own music is the best case for it.
     * Choosing one is, through [selectScene]; the preview stays up behind the paywall.
     */
    fun previewScene(sceneId: String) {
        pendingUnlockSceneId = null
        visual.previewScene(sceneId)
    }

    /** The Styles grid closed over a preview: keep it if the gate allows, else back to the chosen style. */
    fun leaveScenePreview() = visual.leavePreview(allowsStyle)

    /** The chosen style, which a preview on the stage does not change (see [previewScene]). */
    val chosenSceneId: String get() = visual.chosenSceneId

    private val liquidModeState = MutableStateFlow(0)

    /** What Liquid mode ran last frame in the in-app renderer (`liquid::Mode` as an int); see [reportLiquidMode]. */
    val liquidMode: StateFlow<Int> get() = liquidModeState

    /** The live view's renderer copies its native liquid mode here (`AppRoot` binds it), so Compose can read a GL-thread fact. */
    fun reportLiquidMode(mode: Int) {
        liquidModeState.value = mode
    }

    fun applyPreset(preset: Preset) {
        pendingUnlockSceneId = null
        if (allowsStyle(preset.sceneId)) visual.applyPreset(preset) else onStyleLocked()
    }

    /**
     * Randomize everything, the two-circle gesture: a new style, a preset for it, every Customize
     * category rolled. Native decides all of it from one seed (core/viz/RandomizeAll): the style among
     * those [allowsStyle] lets the user keep (Auto's rule, so the paywall never opens), a saved or
     * bundled preset made for it, else on MilkDrop a .milk file, then the roll over that look with
     * Customize's kept controls held. This only lists what the shell knows and applies the pick through
     * [applyPreset] or [selectScene]. [rollParams] false (the roll is Pro) keeps the preset as it is.
     */
    fun randomizeEverything(rollParams: Boolean) {
        // The .milk list is read off the main thread, and only when MilkDrop could be drawn at all.
        if (MilkdropEngine.available && allowsStyle(SceneIds.MILKDROP)) {
            milkPresetFilesAsync { randomizeEverything(rollParams, it) }
        } else {
            randomizeEverything(rollParams, emptyList())
        }
    }

    private fun randomizeEverything(
        rollParams: Boolean,
        milk: List<MilkFile>,
    ) {
        val seed = Random.nextLong()
        val viz = _vizState.value
        val locked = Catalogue.styles.map { it.id }.filterNot(allowsStyle)
        val picks = IntArray(2)
        val style =
            GeodeNative.randomizeAllPick(
                seed,
                chosenSceneId,
                locked.joinToString("\n"),
                viz.presets.joinToString("\n") { it.sceneId },
                milk.size,
                picks,
            ) ?: return
        val preset = viz.presets.getOrNull(picks[0])
        if (preset != null) applyPreset(preset) else selectScene(style)
        milk.getOrNull(picks[1])?.let { visual.emitApply(VizApply(milkPath = it.path, sceneId = style)) }
        modulation.randomizeAllRoll(viz.params, seed, rollParams)
    }

    val activeMilkPath: StateFlow<String?> get() = vizStateStore.activeMilkPath

    fun importPresetFile(
        uri: Uri,
        onResult: (String?) -> Unit,
    ) = presetLibrary.importPresetFile(uri, onResult)

    /** Renames a user preset and keeps its place in the visual playlist. */
    fun renamePreset(
        from: String,
        to: String,
    ): String? = presetLibrary.renamePreset(from, to)?.also { autoVisuals.renamePresetInPlaylist(from, it) }

    fun saveCurrentAsTemplate(
        name: String,
        customShader: String?,
        onResult: (TemplateWrite) -> Unit,
    ) = templateLibrary.saveCurrentAsTemplate(name, customShader, onResult)

    fun adoptTemplate(
        starter: VideoTemplate,
        onResult: (TemplateImport) -> Unit,
    ) = templateLibrary.adopt(starter, onResult)

    fun importTemplateText(
        text: String,
        onResult: (TemplateImport) -> Unit,
    ) = templateLibrary.importTemplateText(text, onResult)

    fun importTemplateFile(
        uri: Uri,
        onResult: (TemplateImport) -> Unit,
    ) = templateLibrary.importTemplateFile(uri, onResult)

    internal val exportController: ExportController =
        ExportController(
            application,
            scope,
            object : ExportController.Host {
                override val exportUri: Uri? get() = currentUri
                override val sceneId: String get() = _vizState.value.sceneId
                override val look =
                    LookSource {
                        val m = modulation
                        liveLook(
                            _vizState.value,
                            m.lfos.value,
                            m.adsrs.value,
                            m.reactions.value,
                            settings.guiPrefs.value,
                            SystemMotion.isReduced(application.contentResolver),
                        )
                    }

                override fun allowsStyle(sceneId: String) = this@PlayerSession.allowsStyle(sceneId)

                override fun onStyleLocked() = this@PlayerSession.onStyleLocked()

                override fun renderLimits() = this@PlayerSession.renderLimits()
            },
        )

    internal val editor: EditorController = EditorController(EditorProjectStore(application), storeScope).also { it.start() }

    // W02: the background image behind the scene - see BackgroundController for why it takes no Host.
    internal val backgroundController: BackgroundController =
        BackgroundController(application, storeScope, BackgroundPrefsStore(prefsFiles.background)).also { it.start() }

    val backgroundPush: StateFlow<BackgroundPushState> get() = backgroundController.push

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
        codec: ExportCodec = ExportCodec.H264,
        watermark: Boolean = false,
    ) = exportController.startExport(aspect, fps, sceneFactory, destination, range, codec, watermark)

    fun deleteStudioClip(
        uri: String,
        onResult: (Boolean) -> Unit,
    ) = exportController.deleteStudioClip(uri, onResult)

    /**
     * Tears the session down. Called only by [dev.geode.di.PlayerSessionProvider], on behalf of the
     * one ViewModel that owns the session — never from a screen.
     */
    internal fun shutdown() {
        // Queue every outstanding write BEFORE the pending set is snapshotted below, or the store
        // scope's cancellation races the very writes this teardown exists to preserve.
        listening.flushListenTime()
        storeScope.launch { vizStateStore.flushIfDirty() }
        val pendingStoreWrites =
            storeScope.coroutineContext.job.children
                .toList()

        captureController.shutdown()
        playerListener?.let { player.removeListener(it) }
        queueController.release()
        playerListener = null
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
        presetLibrary.refreshInitial()
        templateLibrary.refreshInitial()
        musicLibrary.refresh()
        textureController.refresh()
        // The home-screen widget shows the chosen style, and its style buttons step it like the stage swipe.
        // Only the style a burst of changes lands on: each publish is a prefs write and a widget redraw.
        scope.launch {
            vizState.map { chosenSceneId }.distinctUntilChanged().collectLatest {
                delay(WIDGET_STYLE_SETTLE_MS)
                WidgetStyles.publish(application, it)
            }
        }
        scope.launch {
            WidgetStyles.steps(application).collect { step ->
                nextStageStyle(stageStyleOrder(), chosenSceneId, step) { !allowsStyle(it) }?.let(::selectScene)
            }
        }
        // The playback session records plays (#371); this only tells history screens to re-read.
        scope.launch { playback.playsRecorded.drop(1).collect { listening.onPlayRecorded() } }
        scope.launch {
            attachPlayback()
            pollPlaybackState()
        }
    }

    private suspend fun attachPlayback() {
        val pp = settings.loadedPlayerPrefs()
        if (withContext(Dispatchers.IO) { BehaviourStore(prefsFiles.general).load().rememberShuffleRepeat }) {
            player.shuffleModeEnabled = pp.shuffle
            player.repeatMode = pp.repeatMode
        }
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
                        onTrackChanged()
                        bookmarks.onTrackStarted(player)
                    }
                }

                override fun onAudioSessionIdChanged(audioSessionId: Int) {
                    audioFxController.attach(audioSessionId)
                    settings.refreshAudioFx()
                }

                override fun onPlayerError(error: PlaybackException) {
                    // The session has already skipped or stopped (PlaybackErrorRecovery); say what happened.
                    val failure = playback.errors.last ?: return
                    _playbackNotice.value = PlaybackErrors.describe(failure.errorCode, failure.title, failure.action)
                }

                override fun onIsPlayingChanged(isPlaying: Boolean) {
                    if (!isPlaying) return
                    if (externalAudio.value.active) stopExternalAudio()
                    fades.ensureAudibleAfterExternalPlay()
                    PlaybackService.ensureRunning(application)
                }
            }
        playerListener = listener
        player.addListener(listener)
        audioFxController.attach(playback.player.audioSessionId)
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
            bookmarks.onPoll(player)
            delay(POLL_INTERVAL_MS)
        }
    }

    private companion object {
        const val STORE_FLUSH_BUDGET_MS = 2_000L
        const val POLL_INTERVAL_MS = 500L
        const val WIDGET_STYLE_SETTLE_MS = 400L
    }
}
