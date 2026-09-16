package dev.geode.ui

import android.app.Application
import android.net.Uri
import dev.geode.analysis.FeatureTimeline
import dev.geode.data.ExportPrefsStore
import dev.geode.data.GeodePrefsFiles
import dev.geode.data.PerformanceTake
import dev.geode.editor.AnimatableParams
import dev.geode.editor.KeyframeSheet
import dev.geode.export.ExportAspect
import dev.geode.export.ExportCodec
import dev.geode.export.ExportRange
import dev.geode.export.ExportRun
import dev.geode.export.ExportService
import dev.geode.export.LongFormAudio
import dev.geode.export.LoopExtend
import dev.geode.export.LoopRender
import dev.geode.export.LoopSpec
import dev.geode.export.LoudnessAdvice
import dev.geode.export.LoudnessTarget
import dev.geode.export.MixClip
import dev.geode.export.ProjectComposition
import dev.geode.export.StillExporter
import dev.geode.export.TimeOfDayDrift
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
    /**
     * How the last completed render's loudness compared to the persisted loudness-target default,
     * or null before a render finishes (or if it could not be measured). Geode does not yet apply
     * the resulting gain, so this is a readout, not a correction — see [ExportController.startExport].
     */
    val loudnessAdvice: LoudnessAdvice? = null,
)

/** Progress and result of a loop render + long-form extend, mirroring [ExportUiState]. */
data class LoopUiState(
    val phase: ExportPhase = ExportPhase.Idle,
)

/** The state of [ExportController.saveStillFrame] — the still export's own, smaller phase. */
sealed interface StillPhase {
    data object Idle : StillPhase

    data object Running : StillPhase

    data class Done(
        val uri: Uri,
    ) : StillPhase

    data class Failed(
        val message: String,
    ) : StillPhase
}

internal val StillPhase.isBusy: Boolean get() = this is StillPhase.Running

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

        /** The playhead a still export renders at — the same position the transport shows. */
        val positionMs: Long

        fun lfoConfigs(): List<dev.geode.render.LfoConfig>

        fun adsrConfigs(): List<dev.geode.render.AdsrConfig>

        /** The open editor project's tracks; programme-level scene tracks animate the export. */
        fun keyframes(): KeyframeSheet

        suspend fun loadExportTake(): PerformanceTake.Timeline?

        fun publishSections(
            uri: Uri,
            timeline: FeatureTimeline,
        )
    }

    private val exporter = VideoExporter(application)
    private val studioExporter = dev.geode.export.StudioExporter(application)
    private val stillExporter = StillExporter(application)

    private val _exportState = MutableStateFlow(ExportUiState())
    val exportState: StateFlow<ExportUiState> = _exportState

    private val _stillState = MutableStateFlow<StillPhase>(StillPhase.Idle)
    val stillState: StateFlow<StillPhase> = _stillState

    private var stillJob: Job? = null

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
        codec: ExportCodec = ExportCodec.H264,
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
                        host.cachedTimeline ?: host
                            .analyze(uri) { p ->
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
                    val sheet = host.keyframes()
                    val animated =
                        sheet.tracks.any {
                            it.enabled &&
                                it.clipId == null &&
                                AnimatableParams
                                    .find(
                                        it.paramId,
                                    )?.isScene == true
                        }
                    val clipStartMs = range?.startMs ?: 0L
                    val paramsAt: ((Long) -> SceneParams)? =
                        if (exportTake == null && !animated) {
                            null
                        } else {
                            { ms: Long ->
                                val base =
                                    exportTake?.stateAt(clipStartMs + ms - exportTake.trackOffsetMs)?.params ?: host.sceneParams
                                if (animated) AnimatableParams.applyToScene(base, sheet.valuesAt(clipStartMs + ms)) else base
                            }
                        }
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
                            paramsAt = paramsAt,
                            loopSafe = loopSafe,
                            range = range,
                            destination = destination,
                            codec = codec,
                            loudnessTarget = defaultLoudnessTarget(),
                            onProgress = { p ->
                                val overall = 0.2f + p * 0.8f
                                _exportState.update { it.copy(phase = ExportPhase.Running(overall)) }
                                ExportRun.publish(overall)
                            },
                            isCancelled = { exportCancelled || ExportRun.cancelRequested },
                        )
                    _exportState.value =
                        ExportUiState(
                            customDestination = destination != null,
                            phase = result.toPhase(),
                            loudnessAdvice = (result as? VideoExporter.Result.Saved)?.loudnessAdvice,
                        )
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

    /**
     * Renders one frame of the current scene at [host]'s current playback position and saves it
     * as a PNG — the "save this frame" counterpart to [startExport]. Cheap enough, and rare
     * enough, that it does not share [ExportRun]'s single-render guard or foreground notification
     * with a video/loop export; it can run alongside one.
     */
    fun saveStillFrame(
        aspect: ExportAspect,
        sceneFactory: SceneFactory,
        destination: Uri? = null,
    ) {
        val uri = host.exportUri ?: return
        if (_stillState.value.isBusy) return
        _stillState.value = StillPhase.Running
        stillJob =
            scope.launch(Dispatchers.Default) {
                try {
                    val timeline =
                        host.cachedTimeline ?: host
                            .analyze(uri) { }
                            .also { if (host.exportUri == uri) host.cachedTimeline = it }
                    val gui = host.guiPrefs
                    val name = "geode_still_${System.currentTimeMillis()}.png"
                    val result =
                        stillExporter.export(
                            timeline = timeline,
                            sceneFactory = sceneFactory,
                            aspect = aspect,
                            sceneParams = host.sceneParams,
                            positionMs = host.positionMs,
                            fileName = name,
                            lfoConfigs = host.lfoConfigs(),
                            adsrConfigs = host.adsrConfigs(),
                            reducedMotion = gui.reducedMotion,
                            destination = destination,
                        )
                    _stillState.value =
                        when (result) {
                            is StillExporter.Result.Saved -> StillPhase.Done(result.uri)
                            is StillExporter.Result.Failed -> StillPhase.Failed(result.message)
                        }
                } catch (t: kotlinx.coroutines.CancellationException) {
                    _stillState.value = StillPhase.Idle
                    throw t
                } catch (t: Throwable) {
                    _stillState.value = StillPhase.Failed("${t.javaClass.simpleName}: ${t.message ?: "no message"}")
                }
            }
    }

    fun resetStillState() {
        if (!_stillState.value.isBusy) _stillState.value = StillPhase.Idle
    }

    fun refreshStudioClips() {
        scope.launch {
            _studio.update { it.copy(phase = ExportPhase.Loading) }
            val clips =
                withContext(Dispatchers.IO) {
                    dev.geode.export.StudioClips
                        .list(application)
                }
            _studio.update { it.copy(clips = clips, phase = ExportPhase.Idle) }
        }
    }

    fun deleteStudioClip(
        uri: String,
        onResult: (Boolean) -> Unit,
    ) {
        scope.launch {
            val ok =
                withContext(Dispatchers.IO) {
                    dev.geode.export.StudioClips
                        .delete(application, uri)
                }
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
            val ok =
                withContext(Dispatchers.IO) {
                    dev.geode.export.StudioClips
                        .rename(application, uri, name)
                }
            if (ok) refreshStudioClips()
            onResult(ok)
        }
    }

    fun describeStudioClip(
        uri: Uri,
        onReady: (dev.geode.export.StudioClip) -> Unit,
    ) {
        scope.launch {
            val clip =
                withContext(Dispatchers.IO) {
                    dev.geode.export.StudioClips
                        .describe(application, uri)
                }
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
                        codec = defaultCodec(),
                    ) { p -> _studio.update { it.copy(phase = ExportPhase.Running(p.coerceIn(0f, 1f))) } }
                _studio.update { it.copy(phase = result.toPhase()) }
                refreshStudioClips()
                studioJob = null
            }
    }

    fun startProjectExport(project: dev.geode.editor.EditorProject) {
        if (_studio.value.phase.isBusy) return
        val built = ProjectComposition.build(application, project)
        if (built !is ProjectComposition.Outcome.Ready) {
            _studio.update { it.copy(phase = ExportPhase.Failed(application.getString(dev.geode.R.string.editor_export_no_video))) }
            return
        }
        _studio.update { it.copy(phase = ExportPhase.Running(0f)) }
        studioJob =
            scope.launch {
                val name = "geode_cut_${System.currentTimeMillis()}.mp4"
                val result =
                    studioExporter.exportComposition(built.composition, built.durationMs, name, defaultCodec()) { p ->
                        _studio.update { it.copy(phase = ExportPhase.Running(p.coerceIn(0f, 1f))) }
                    }
                _studio.update { it.copy(phase = result.toPhase()) }
                refreshStudioClips()
                studioJob = null
            }
    }

    private fun defaultCodec(): ExportCodec = ExportPrefsStore(GeodePrefsFiles(application).general).load().codec

    // The main export's UI (SettingsDialog) has no path to this controller's public API for
    // per-render options that aren't already threaded through startExport's callers, so the
    // loudness target rides along as a persisted default instead, the same way defaultCodec()
    // above does for studio exports.
    private fun defaultLoudnessTarget(): LoudnessTarget =
        LoudnessTarget.byId(
            ExportPrefsStore(GeodePrefsFiles(application).general).load().loudnessTargetId,
        )

    fun cancelStudioExport() {
        studioExporter.cancel()
        studioJob?.cancel()
        studioJob = null
        _studio.update { it.copy(phase = ExportPhase.Idle) }
    }

    fun clearStudioResult() {
        _studio.update { it.copy(phase = ExportPhase.Idle) }
    }

    private val loopRenderer = LoopRender(application)
    private val loopExtender = LoopExtend(application)

    private val _loopState = MutableStateFlow(LoopUiState())
    val loopState: StateFlow<LoopUiState> = _loopState

    @Volatile
    private var loopCancelled = false

    private var loopJob: Job? = null

    /**
     * Renders a seamless loop from the current track's analysis, then repeats it into a
     * long-form video with [audioClips] (or, if empty, the current track) as its soundtrack.
     *
     * Runs on [ExportRun.scope] and shares [ExportRun]'s single-render guard and foreground
     * notification with [startExport]: a long-form render is exactly the case that must survive
     * the Activity going away, and the two are heavy enough that they should not overlap.
     */
    fun startLoopRender(
        aspect: ExportAspect,
        codec: ExportCodec,
        fps: Int,
        sceneFactory: SceneFactory,
        loopMs: Long,
        crossfadeMs: Long,
        drift: TimeOfDayDrift,
        audioClips: List<Uri>,
        destination: Uri? = null,
    ) {
        val uri = host.exportUri ?: return
        if (_loopState.value.phase.isBusy || ExportRun.running) return
        loopCancelled = false
        _loopState.value = LoopUiState(phase = ExportPhase.Running(0f))
        ExportRun.begin(uri.lastPathSegment.orEmpty().substringAfterLast('/'))
        ExportService.start(application)
        loopJob =
            ExportRun.scope.launch(Dispatchers.Default) {
                try {
                    val analysed =
                        host.cachedTimeline ?: host
                            .analyze(uri) { p ->
                                publishLoopProgress(p * ANALYSIS_SPAN)
                            }.also { if (host.exportUri == uri) host.cachedTimeline = it }
                    val gui = host.guiPrefs
                    val spec = LoopSpec.of(loopMs, crossfadeMs, fps = fps, bpm = analysed.bpm)
                    val renderResult =
                        loopRenderer.render(
                            timeline = analysed,
                            sceneFactory = sceneFactory,
                            aspect = aspect,
                            spec = spec,
                            sceneParams = host.sceneParams,
                            drift = drift,
                            lfoConfigs = host.lfoConfigs(),
                            adsrConfigs = host.adsrConfigs(),
                            reducedMotion = gui.reducedMotion,
                            codec = codec,
                            onProgress = { p -> publishLoopProgress(ANALYSIS_SPAN + p * RENDER_SPAN) },
                            isCancelled = { loopCancelled || ExportRun.cancelRequested },
                        )
                    _loopState.value = LoopUiState(phase = finishLoopRender(renderResult, uri, audioClips, destination))
                } catch (t: Throwable) {
                    if (t is kotlinx.coroutines.CancellationException) {
                        _loopState.value = LoopUiState()
                        throw t
                    } else if (loopCancelled) {
                        _loopState.value = LoopUiState()
                    } else {
                        val detail = "${t.javaClass.simpleName}: ${t.message ?: "no message"}"
                        _loopState.value = LoopUiState(phase = ExportPhase.Failed(detail))
                    }
                } finally {
                    ExportRun.finish()
                }
            }
    }

    /** Extends a rendered reel into the long-form file, or passes through a render failure. */
    private suspend fun finishLoopRender(
        renderResult: LoopRender.Result,
        trackUri: Uri,
        audioClips: List<Uri>,
        destination: Uri?,
    ): ExportPhase =
        when (renderResult) {
            is LoopRender.Result.Failed -> ExportPhase.Failed(renderResult.message)
            LoopRender.Result.Cancelled -> ExportPhase.Idle
            is LoopRender.Result.Rendered -> {
                val reel = renderResult.reel
                try {
                    val audio =
                        if (audioClips.isEmpty()) {
                            LongFormAudio.SingleTrack(MixClip(trackUri, trackUri.lastPathSegment.orEmpty().substringAfterLast('/')))
                        } else {
                            LongFormAudio.Mix(
                                audioClips.map { clip ->
                                    MixClip(clip, clip.lastPathSegment.orEmpty().substringAfterLast('/'))
                                },
                            )
                        }
                    val name = "geode_loop_${System.currentTimeMillis()}.mp4"
                    val extended =
                        loopExtender.extend(
                            reel = reel,
                            audio = audio,
                            fileName = name,
                            destination = destination,
                            onProgress = { p -> publishLoopProgress(ANALYSIS_SPAN + RENDER_SPAN + p * EXTEND_SPAN) },
                            isCancelled = { loopCancelled || ExportRun.cancelRequested },
                        )
                    when (extended) {
                        is LoopExtend.Result.Saved -> ExportPhase.Done(extended.uri)
                        is LoopExtend.Result.Failed -> ExportPhase.Failed(extended.message)
                        LoopExtend.Result.Cancelled -> ExportPhase.Idle
                    }
                } finally {
                    reel.delete()
                }
            }
        }

    private fun publishLoopProgress(overall: Float) {
        val clamped = overall.coerceIn(0f, 1f)
        _loopState.update { it.copy(phase = ExportPhase.Running(clamped)) }
        ExportRun.publish(clamped)
    }

    fun cancelLoopRender() {
        loopCancelled = true
    }

    fun clearLoopResult() {
        if (!_loopState.value.phase.isBusy) _loopState.value = LoopUiState()
    }

    private companion object {
        // Analysing the track is quick against the render itself; extending mostly copies
        // already-encoded samples, so it gets less of the bar than the GPU render does.
        const val ANALYSIS_SPAN = 0.1f
        const val RENDER_SPAN = 0.6f
        const val EXTEND_SPAN = 1f - ANALYSIS_SPAN - RENDER_SPAN
    }
}
