package dev.geode.ui

import android.app.Application
import android.net.Uri
import androidx.core.net.toUri
import dev.geode.data.BackgroundPrefsStore
import dev.geode.data.ExportPrefsStore
import dev.geode.data.GeodePrefsFiles
import dev.geode.data.GuiPrefs
import dev.geode.data.PerformanceTake
import dev.geode.editor.AnimatableParams
import dev.geode.editor.KeyframeSheet
import dev.geode.export.BatchOutcome
import dev.geode.export.BatchTrack
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
import dev.geode.export.RenderBatch
import dev.geode.export.TimeOfDayDrift
import dev.geode.export.VideoExporter
import dev.geode.render.SceneFactory
import dev.geode.render.scene.SceneParams
import dev.geode.viz.BackgroundExportSpec
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
    /** False until the first clip list arrives, so the empty state doesn't flash first. Kept apart from [phase], which holds the last export's result. */
    val clipsLoaded: Boolean = false,
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
    /**
     * The finished file's name in Movies/MuzViz, so the Render bar can say what was made. Null for a
     * folder the user picked: that name is the picker's, and the result URI is a SAF document id.
     */
    val fileName: String? = null,
    /** The batch under way or just finished, or null for a single render. */
    val batch: RenderBatch? = null,
)

/** Progress and result of a loop render + long-form extend, mirroring [ExportUiState]. */
data class LoopUiState(
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

        val guiPrefs: GuiPrefs

        val sceneId: String
        val sceneParams: SceneParams

        fun lfoConfigs(): List<dev.geode.render.LfoConfig>

        fun adsrConfigs(): List<dev.geode.render.AdsrConfig>

        fun reactions(): List<dev.geode.render.ReactionRoute>?

        /** The open editor project's tracks; programme-level scene tracks animate the export. */
        fun keyframes(): KeyframeSheet

        suspend fun loadExportTake(): PerformanceTake.Timeline?
    }

    private val exporter = VideoExporter(application)
    private val studioExporter = dev.geode.export.StudioExporter(application)

    private val _exportState = MutableStateFlow(ExportUiState())
    val exportState: StateFlow<ExportUiState> = _exportState

    private val _studio = MutableStateFlow(StudioUiState())

    val studio: StateFlow<StudioUiState> = _studio

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
        range: ExportRange? = null,
        sceneFactoryFor: ((String) -> SceneFactory)? = null,
        codec: ExportCodec = ExportCodec.H264,
        watermark: Boolean = false,
    ) {
        val uri = host.exportUri ?: return
        if (_exportState.value.phase.isBusy || ExportRun.running) return
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
                    val gui = host.guiPrefs
                    val name = "muzviz_${System.currentTimeMillis()}.mp4"
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
                            sceneFactory = factory,
                            aspect = aspect,
                            fileName = name,
                            sceneParams = host.sceneParams,
                            lfoConfigs = host.lfoConfigs(),
                            adsrConfigs = host.adsrConfigs(),
                            reactions = host.reactions(),
                            reducedMotion = gui.reducedMotion,
                            requestedFps = fps,
                            soundTuning = gui.soundTuning,
                            paramsAt = paramsAt,
                            range = range,
                            destination = destination,
                            codec = codec,
                            loudnessTarget = defaultLoudnessTarget(),
                            background = defaultBackground(),
                            onProgress = { p ->
                                _exportState.update { it.copy(phase = ExportPhase.Running(p)) }
                                ExportRun.publish(p, steady = p >= VideoExporter.AUDIO_PROGRESS_SPAN)
                            },
                            isCancelled = { ExportRun.cancelRequested },
                            watermark = watermark,
                        )
                    _exportState.value =
                        ExportUiState(
                            customDestination = destination != null,
                            phase = result.toPhase(),
                            loudnessAdvice = (result as? VideoExporter.Result.Saved)?.loudnessAdvice,
                            fileName = name.takeIf { destination == null },
                        )
                } catch (t: Throwable) {
                    // Cancellation is tested before the user's own cancel flag: when the two
                    // coincide the flag branch used to win and swallow the CancellationException,
                    // leaving a cancelled coroutine to finish as if it had succeeded.
                    if (t is kotlinx.coroutines.CancellationException) {
                        _exportState.value = ExportUiState()
                        throw t
                    } else if (ExportRun.cancelRequested) {
                        _exportState.value = ExportUiState()
                    } else {
                        val reason = t.message ?: application.getString(dev.geode.R.string.export_error_no_message)
                        val detail = "${t.javaClass.simpleName}: $reason"
                        _exportState.value = ExportUiState(phase = ExportPhase.Failed(detail))
                    }
                } finally {
                    ExportRun.finish()
                }
            }
    }

    /**
     * Renders [tracks] one after another through the same exporter as [startExport], each to its
     * own `muzviz_*.mp4` in Movies/MuzViz, under one [ExportRun] so the foreground service and its
     * notification cover the whole batch. A track that fails is recorded and the next one starts.
     * Cancel stops the track rendering now, the same way it stops a single render, and marks it
     * and every track after it cancelled; files already finished stay. Every track uses
     * [sceneParams] (the current look, or the preset the user chose), whole length, with no performance take or editor keyframes:
     * those belong to the track playing now, not to the rest of a playlist.
     */
    fun startBatch(
        tracks: List<BatchTrack>,
        aspect: ExportAspect,
        fps: Int,
        sceneFactory: SceneFactory,
        sceneParams: SceneParams?,
        codec: ExportCodec,
        watermark: Boolean,
    ) {
        if (tracks.isEmpty() || _exportState.value.phase.isBusy || ExportRun.running) return
        var batch = RenderBatch(tracks)
        _exportState.value = ExportUiState(phase = ExportPhase.Running(0f), batch = batch)
        ExportRun.begin(application.getString(dev.geode.R.string.render_batch_label, tracks.size))
        ExportService.start(application)
        exportJob =
            ExportRun.scope.launch(Dispatchers.Default) {
                try {
                    val gui = host.guiPrefs
                    // A Cancel between tracks ends the loop; the tracks left are then marked cancelled.
                    var track = batch.current
                    while (track != null && !ExportRun.cancelRequested) {
                        val publish: (Float) -> Unit = { within ->
                            val overall = batch.progress(within)
                            _exportState.update { it.copy(phase = ExportPhase.Running(overall), batch = batch) }
                            ExportRun.publish(overall)
                        }
                        publish(0f)
                        val outcome =
                            try {
                                renderBatchTrack(track, aspect, fps, sceneFactory, sceneParams, codec, watermark, gui, publish)
                            } catch (c: kotlinx.coroutines.CancellationException) {
                                throw c
                            } catch (t: Throwable) {
                                BatchOutcome.Failed(t.message ?: t.javaClass.simpleName)
                            }
                        batch = if (outcome == BatchOutcome.Cancelled) batch.cancelRest() else batch.record(outcome)
                        track = batch.current
                    }
                    _exportState.value = batch.cancelRest().toUiState()
                } catch (t: kotlinx.coroutines.CancellationException) {
                    _exportState.value = batch.cancelRest().toUiState()
                    throw t
                } finally {
                    ExportRun.finish()
                }
            }
    }

    private suspend fun renderBatchTrack(
        track: BatchTrack,
        aspect: ExportAspect,
        fps: Int,
        sceneFactory: SceneFactory,
        sceneParams: SceneParams?,
        codec: ExportCodec,
        watermark: Boolean,
        gui: GuiPrefs,
        publish: (Float) -> Unit,
    ): BatchOutcome {
        val uri = track.uri.toUri()
        val name = "muzviz_${System.currentTimeMillis()}.mp4"
        val result =
            exporter.export(
                audioUri = uri,
                sceneFactory = sceneFactory,
                aspect = aspect,
                fileName = name,
                sceneParams = sceneParams ?: host.sceneParams,
                lfoConfigs = host.lfoConfigs(),
                adsrConfigs = host.adsrConfigs(),
                reactions = host.reactions(),
                reducedMotion = gui.reducedMotion,
                requestedFps = fps,
                soundTuning = gui.soundTuning,
                codec = codec,
                loudnessTarget = defaultLoudnessTarget(),
                background = defaultBackground(),
                onProgress = publish,
                isCancelled = { ExportRun.cancelRequested },
                watermark = watermark,
            )
        return when (result) {
            is VideoExporter.Result.Saved -> BatchOutcome.Saved(result.uri.toString(), name)
            is VideoExporter.Result.Failed -> BatchOutcome.Failed(result.message)
            VideoExporter.Result.Cancelled -> BatchOutcome.Cancelled
        }
    }

    private fun RenderBatch.toUiState(): ExportUiState {
        val last = lastSaved
        return ExportUiState(
            phase =
                when {
                    last != null -> ExportPhase.Done(last.uri.toUri())
                    failures.isNotEmpty() -> ExportPhase.Failed(failures.first().second)
                    else -> ExportPhase.Idle
                },
            fileName = last?.fileName,
            batch = this,
        )
    }

    // The job runs in the process-wide ExportRun.scope and can outlive this controller (Back mid-render,
    // then back in from the notification builds a new session), so the cancel flag lives there too.
    fun cancelExport() = ExportRun.requestCancel()

    fun resetExportState() {
        if (!_exportState.value.phase.isBusy) _exportState.value = ExportUiState()
    }

    fun refreshStudioClips() {
        scope.launch {
            val clips =
                withContext(Dispatchers.IO) {
                    dev.geode.export.StudioClips
                        .list(application)
                }
            // Clips only: this runs straight after an export, and the phase holds its result.
            _studio.update { it.copy(clips = clips, clipsLoaded = true) }
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
        destination: Uri? = null,
    ) {
        if (_studio.value.phase.isBusy) return
        _studio.update { it.copy(phase = ExportPhase.Running(0f)) }
        studioJob =
            scope.launch {
                val name = "muzviz_studio_${System.currentTimeMillis()}.mp4"
                val result =
                    studioExporter.export(
                        source = Uri.parse(clip.uri),
                        sourceDurationMs = clip.durationMs,
                        edit = edit,
                        displayName = name,
                        codec = defaultCodec(),
                        destination = destination,
                    ) { p -> _studio.update { it.copy(phase = ExportPhase.Running(p.coerceIn(0f, 1f))) } }
                _studio.update { it.copy(phase = result.toPhase()) }
                refreshStudioClips()
                studioJob = null
            }
    }

    fun startProjectExport(
        project: dev.geode.editor.EditorProject,
        destination: Uri? = null,
    ) {
        if (_studio.value.phase.isBusy) return
        _studio.update { it.copy(phase = ExportPhase.Running(0f)) }
        studioJob =
            scope.launch {
                // build() reads the export defaults from prefs, so it runs on IO with them.
                val built = withContext(Dispatchers.IO) { ProjectComposition.build(application, project) }
                if (built !is ProjectComposition.Outcome.Ready) {
                    _studio.update { it.copy(phase = ExportPhase.Failed(application.getString(dev.geode.R.string.editor_export_no_video))) }
                    studioJob = null
                    return@launch
                }
                val name = "muzviz_cut_${System.currentTimeMillis()}.mp4"
                val result =
                    studioExporter.exportComposition(built.composition, built.durationMs, name, defaultCodec(), destination) { p ->
                        _studio.update { it.copy(phase = ExportPhase.Running(p.coerceIn(0f, 1f))) }
                    }
                _studio.update { it.copy(phase = result.toPhase()) }
                refreshStudioClips()
                studioJob = null
            }
    }

    // Called from [scope], which is Main: the prefs read itself goes to IO.
    private suspend fun defaultCodec(): ExportCodec =
        withContext(Dispatchers.IO) { ExportPrefsStore(GeodePrefsFiles(application).general).load().codec }

    // The main export's UI (Studio › Render) has no path to this controller's public API for
    // per-render options that aren't already threaded through startExport's callers, so the
    // loudness target rides along as a persisted default instead, the same way defaultCodec()
    // above does for studio exports.
    private fun defaultLoudnessTarget(): LoudnessTarget =
        LoudnessTarget.byId(
            ExportPrefsStore(GeodePrefsFiles(application).general).load().loudnessTargetId,
        )

    // Same "no per-render option wired through startExport's callers" situation as
    // defaultLoudnessTarget() above: the background image rides along as the persisted default.
    private fun defaultBackground(): BackgroundExportSpec? {
        val p = BackgroundPrefsStore(GeodePrefsFiles(application).background).load()
        val uri = p.uri ?: return null
        return BackgroundExportSpec(uri.toUri(), p.blend, p.amount, p.blurRadius, p.dim)
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

    private val loopRenderer = LoopRender(application)
    private val loopExtender = LoopExtend(application)

    private val _loopState = MutableStateFlow(LoopUiState())
    val loopState: StateFlow<LoopUiState> = _loopState

    private var loopJob: Job? = null

    /**
     * Renders a seamless loop of the current track's visuals, then repeats it into a
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
        watermark: Boolean = false,
    ) {
        val uri = host.exportUri ?: return
        if (_loopState.value.phase.isBusy || ExportRun.running) return
        _loopState.value = LoopUiState(phase = ExportPhase.Running(0f))
        ExportRun.begin(uri.lastPathSegment.orEmpty().substringAfterLast('/'))
        ExportService.start(application)
        loopJob =
            ExportRun.scope.launch(Dispatchers.Default) {
                try {
                    val gui = host.guiPrefs
                    val spec = LoopSpec.of(loopMs, crossfadeMs, fps = fps)
                    val renderResult =
                        loopRenderer.render(
                            sceneFactory = sceneFactory,
                            aspect = aspect,
                            spec = spec,
                            sceneParams = host.sceneParams,
                            drift = drift,
                            lfoConfigs = host.lfoConfigs(),
                            adsrConfigs = host.adsrConfigs(),
                            reactions = host.reactions(),
                            reducedMotion = gui.reducedMotion,
                            codec = codec,
                            audioUri = uri,
                            soundTuning = gui.soundTuning,
                            onProgress = { p -> publishLoopProgress(p * RENDER_SPAN) },
                            isCancelled = { ExportRun.cancelRequested },
                            watermark = watermark,
                        )
                    _loopState.value = LoopUiState(phase = finishLoopRender(renderResult, uri, audioClips, destination))
                } catch (t: Throwable) {
                    if (t is kotlinx.coroutines.CancellationException) {
                        _loopState.value = LoopUiState()
                        throw t
                    } else if (ExportRun.cancelRequested) {
                        _loopState.value = LoopUiState()
                    } else {
                        val reason = t.message ?: application.getString(dev.geode.R.string.export_error_no_message)
                        val detail = "${t.javaClass.simpleName}: $reason"
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
                    val name = "muzviz_loop_${System.currentTimeMillis()}.mp4"
                    val extended =
                        loopExtender.extend(
                            reel = reel,
                            audio = audio,
                            fileName = name,
                            destination = destination,
                            onProgress = { p -> publishLoopProgress(RENDER_SPAN + p * EXTEND_SPAN) },
                            isCancelled = { ExportRun.cancelRequested },
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

    fun cancelLoopRender() = ExportRun.requestCancel()

    fun clearLoopResult() {
        if (!_loopState.value.phase.isBusy) _loopState.value = LoopUiState()
    }

    private companion object {
        // Extending mostly copies already-encoded samples, so it gets less of the bar than the
        // GPU render does.
        const val RENDER_SPAN = 0.7f
        const val EXTEND_SPAN = 1f - RENDER_SPAN
    }
}
