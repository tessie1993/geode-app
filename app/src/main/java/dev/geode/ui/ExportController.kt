package dev.geode.ui

import android.app.Application
import android.net.Uri
import dev.geode.analysis.FeatureTimeline
import dev.geode.data.PerformanceTake
import dev.geode.export.ExportAspect
import dev.geode.export.ExportRange
import dev.geode.export.ExportRun
import dev.geode.export.ExportService
import dev.geode.export.VideoExporter
import dev.geode.render.SceneFactory
import dev.geode.render.scene.SceneParams
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.takeWhile
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

data class StudioUiState(
    val clips: List<dev.geode.export.StudioClip> = emptyList(),
    val phase: ExportPhase = ExportPhase.Idle,
)

data class ExportUiState(
    val customDestination: Boolean = false,
    val phase: ExportPhase = ExportPhase.Idle,
)

internal fun exportSceneIdFor(
    take: PerformanceTake.Timeline?,
    liveSceneId: String,
): String =
    take
        ?.stateAt(0L)
        ?.sceneId
        ?.takeIf { it.isNotEmpty() }
        ?: liveSceneId

@androidx.annotation.OptIn(androidx.media3.common.util.UnstableApi::class)
internal class ExportController(
    private val application: Application,
    private val scope: CoroutineScope,
    private val host: Host,
) {
    interface Host {
        val exportUri: Uri?

        var cachedTimeline: FeatureTimeline?

        suspend fun analyze(
            uri: Uri,
            onProgress: (Float) -> Unit,
        ): FeatureTimeline

        val guiPrefs: GuiPrefs

        val sceneId: String
        val sceneParams: SceneParams

        fun lfoConfigs(): List<dev.geode.render.LfoConfig>

        fun adsrConfigs(): List<dev.geode.render.AdsrConfig>

        suspend fun loadExportTake(): PerformanceTake.Timeline?

        fun publishSections(
            uri: Uri,
            timeline: FeatureTimeline,
        )
    }

    private val exporter = VideoExporter(application)
    private val studioExporter = dev.geode.export.StudioExporter(application)

    private val _exportState = MutableStateFlow(ExportUiState())
    val exportState: StateFlow<ExportUiState> = _exportState

    private val _studio = MutableStateFlow(StudioUiState())

    val studio: StateFlow<StudioUiState> = _studio

    @Volatile
    private var exportCancelled = false

    private var exportJob: Job? = null

    init {
        if (ExportRun.running) {
            scope.launch {
                ExportRun.state
                    .takeWhile { it.running }
                    .collect { run ->
                        _exportState.update { it.copy(phase = ExportPhase.Running(run.progress ?: 0f)) }
                    }
                if (!ExportRun.running) _exportState.value = ExportUiState()
            }
        }
    }

    private var studioJob: Job? = null

    fun startExport(
        aspect: ExportAspect,
        fps: Int,
        sceneFactory: SceneFactory,
        destination: Uri? = null,
        loopSafe: Boolean = false,
        range: ExportRange? = null,
        sceneFactoryFor: ((String) -> SceneFactory)? = null,
    ) {
        val uri = host.exportUri ?: return
        if (_exportState.value.phase.isBusy || ExportRun.running) return
        exportCancelled = false
        _exportState.value =
            ExportUiState(customDestination = destination != null, phase = ExportPhase.Running(0f))
        // Basename only: a SAF document id is "primary:Music/Artist/Song.mp3", and this
        // label goes straight into the foreground-service notification, where the lock
        // screen and every enabled notification listener can read it.
        ExportRun.begin(uri.lastPathSegment.orEmpty().substringAfterLast('/'))
        ExportService.start(application)
        exportJob =
            ExportRun.scope.launch(Dispatchers.Default) {
                try {
                    val analysed =
                        host.cachedTimeline ?: host.analyze(uri) { p ->
                            _exportState.update { it.copy(phase = ExportPhase.Running(p * 0.2f)) }
                            ExportRun.publish(p * 0.2f)
                        }.also { if (host.exportUri == uri) host.cachedTimeline = it }
                    val gui = host.guiPrefs
                    val t =
                        analysed.withBeatSensitivity(
                            gui.beatSensitivity,
                            gui.effectiveBeatMinIntervalMs,
                        )
                    host.publishSections(uri, t)
                    val name = "geode_${System.currentTimeMillis()}.mp4"
                    val exportTake = host.loadExportTake()
                    val factory =
                        if (exportTake != null && sceneFactoryFor != null) {
                            sceneFactoryFor(exportSceneIdFor(exportTake, host.sceneId))
                        } else {
                            sceneFactory
                        }
                    val result =
                        exporter.export(
                            audioUri = uri,
                            timeline = t,
                            sceneFactory = factory,
                            aspect = aspect,
                            fileName = name,
                            sceneParams = host.sceneParams,
                            lfoConfigs = host.lfoConfigs(),
                            adsrConfigs = host.adsrConfigs(),
                            reducedMotion = gui.reducedMotion,
                            requestedFps = fps,
                            paramsAt =
                                exportTake?.let { take ->
                                    val clipStartMs = range?.startMs ?: 0L
                                    { ms: Long ->
                                        take.stateAt(clipStartMs + ms - take.trackOffsetMs)?.params
                                            ?: host.sceneParams
                                    }
                                },
                            loopSafe = loopSafe,
                            range = range,
                            destination = destination,
                            onProgress = { p ->
                                val overall = 0.2f + p * 0.8f
                                _exportState.update { it.copy(phase = ExportPhase.Running(overall)) }
                                ExportRun.publish(overall)
                            },
                            isCancelled = { exportCancelled || ExportRun.cancelRequested },
                        )
                    _exportState.value =
                        ExportUiState(customDestination = destination != null, phase = result.toPhase())
                } catch (t: Throwable) {
                    // Cancellation is tested before the user's own cancel flag: when the two
                    // coincide the flag branch used to win and swallow the CancellationException,
                    // leaving a cancelled coroutine to finish as if it had succeeded.
                    if (t is kotlinx.coroutines.CancellationException) {
                        _exportState.value = ExportUiState()
                        throw t
                    } else if (exportCancelled) {
                        _exportState.value = ExportUiState()
                    } else {
                        val detail = "${t.javaClass.simpleName}: ${t.message ?: "no message"}"
                        _exportState.value = ExportUiState(phase = ExportPhase.Failed(detail))
                    }
                } finally {
                    ExportRun.finish()
                }
            }
    }

    fun cancelExport() {
        exportCancelled = true
    }

    fun resetExportState() {
        if (!_exportState.value.phase.isBusy) _exportState.value = ExportUiState()
    }

    fun refreshStudioClips() {
        scope.launch {
            _studio.update { it.copy(phase = ExportPhase.Loading) }
            val clips = withContext(Dispatchers.IO) { dev.geode.export.StudioClips.list(application) }
            _studio.update { it.copy(clips = clips, phase = ExportPhase.Idle) }
        }
    }

    fun deleteStudioClip(
        uri: String,
        onResult: (Boolean) -> Unit,
    ) {
        scope.launch {
            val ok = withContext(Dispatchers.IO) { dev.geode.export.StudioClips.delete(application, uri) }
            if (ok) refreshStudioClips()
            onResult(ok)
        }
    }

    fun renameStudioClip(
        uri: String,
        name: String,
        onResult: (Boolean) -> Unit,
    ) {
        scope.launch {
            val ok = withContext(Dispatchers.IO) { dev.geode.export.StudioClips.rename(application, uri, name) }
            if (ok) refreshStudioClips()
            onResult(ok)
        }
    }

    fun describeStudioClip(
        uri: Uri,
        onReady: (dev.geode.export.StudioClip) -> Unit,
    ) {
        scope.launch {
            val clip = withContext(Dispatchers.IO) { dev.geode.export.StudioClips.describe(application, uri) }
            onReady(clip)
        }
    }

    fun startStudioExport(
        clip: dev.geode.export.StudioClip,
        edit: dev.geode.export.ClipEdit,
    ) {
        if (_studio.value.phase.isBusy) return
        _studio.update { it.copy(phase = ExportPhase.Running(0f)) }
        studioJob =
            scope.launch {
                val name = "geode_studio_${System.currentTimeMillis()}.mp4"
                val result =
                    studioExporter.export(
                        source = Uri.parse(clip.uri),
                        sourceDurationMs = clip.durationMs,
                        edit = edit,
                        displayName = name,
                    ) { p -> _studio.update { it.copy(phase = ExportPhase.Running(p.coerceIn(0f, 1f))) } }
                _studio.update { it.copy(phase = result.toPhase()) }
                refreshStudioClips()
                studioJob = null
            }
    }

    fun cancelStudioExport() {
        studioExporter.cancel()
        studioJob?.cancel()
        studioJob = null
        _studio.update { it.copy(phase = ExportPhase.Idle) }
    }

    fun clearStudioResult() {
        _studio.update { it.copy(phase = ExportPhase.Idle) }
    }
}
