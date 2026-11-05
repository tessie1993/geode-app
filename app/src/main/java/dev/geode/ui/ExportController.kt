package dev.geode.ui

import android.app.Application
import android.net.Uri
import android.os.Build
import android.provider.DocumentsContract
import android.widget.Toast
import androidx.core.net.toUri
import dev.geode.RingLog
import dev.geode.data.BackgroundPrefsStore
import dev.geode.data.EditorProjectStore
import dev.geode.data.ExportDefaults
import dev.geode.data.ExportPrefsStore
import dev.geode.data.GeodePrefsFiles
import dev.geode.data.GuiPrefs
import dev.geode.data.PerformanceTake
import dev.geode.data.SaveTarget
import dev.geode.editor.AnimatableParams
import dev.geode.editor.BeatCut
import dev.geode.editor.Clip
import dev.geode.editor.ClipContent
import dev.geode.editor.ClipId
import dev.geode.editor.EditorProject
import dev.geode.editor.KeyframeSheet
import dev.geode.editor.Lane
import dev.geode.editor.LaneId
import dev.geode.editor.LaneKind
import dev.geode.editor.ProjectId
import dev.geode.editor.ProjectRender
import dev.geode.editor.StudioProject
import dev.geode.editor.Timeline
import dev.geode.editor.sourceTimeAt
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
import dev.geode.export.ProjectComposition.SceneSegment
import dev.geode.export.RenderBatch
import dev.geode.export.RenderScope
import dev.geode.export.StudioExporter
import dev.geode.export.TimeOfDayDrift
import dev.geode.export.VideoExporter
import dev.geode.render.SceneFactory
import dev.geode.render.bridge.StyleSchedule
import dev.geode.render.scene.Catalogue
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
import java.io.File

data class StudioUiState(
    val clips: List<dev.geode.export.StudioClip> = emptyList(),
    val phase: ExportPhase = ExportPhase.Idle,
    /**
     * What the last project render left out and why (the render plan's skipped-clip report), or
     * null when it drew everything. It is also shown as a toast when the render ends; this copy is
     * for a Render page that wants to keep it on screen.
     */
    val notice: String? = null,
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

        /**
         * The open editor project's tracks. Since every render renders a project (#232) the one-tap
         * render no longer borrows them: it is a project of its own, and the open project's keyframes
         * render with that project. Kept for the Host contract until PlayerSession drops it.
         */
        fun keyframes(): KeyframeSheet

        suspend fun loadExportTake(): PerformanceTake.Timeline?

        /** Whether the entitlement allows rendering [sceneId] (FeatureGate.allowsStyle). */
        fun allowsStyle(sceneId: String): Boolean

        /** Offers Pro for a locked style; safe from any thread (the paywall request is a StateFlow). */
        fun onStyleLocked()
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

    @Volatile
    private var studioCancelled = false

    // Built on first use, which is on IO: the store sweeps old folders when it is made.
    private val projectStore by lazy { EditorProjectStore(application) }

    /**
     * The one-tap Render: the current track as a project with one visual lane over the song (and
     * the export take, when one is chosen, as that lane's clip), rendered through the same path as
     * every Studio project ([render]). That project is always a direct one, so the visualizer
     * exporter writes the file itself, as it always has.
     */
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
        exportCancelled = false
        _exportState.value =
            ExportUiState(customDestination = destination != null, phase = ExportPhase.Running(0f))
        // Basename only: a SAF document id is "primary:Music/Artist/Song.mp3", and this
        // label goes straight into the foreground-service notification, where the lock
        // screen and every enabled notification listener can read it.
        ExportRun.begin(uri.lastPathSegment.orEmpty().substringAfterLast('/'))
        ExportService.start(application)
        exportJob =
            ExportRun.launch {
                try {
                    val name = "muzviz_${System.currentTimeMillis()}.mp4"
                    val exportTake = host.loadExportTake()
                    val sceneId =
                        if (exportTake != null && sceneFactoryFor != null) {
                            exportSceneIdFor(exportTake, host.sceneId)
                        } else {
                            sceneFactory.sceneId
                        }
                    val project = oneTapProject(uri, range, sceneId, sceneFactory.milkPresetPath, exportTake)
                    val factoryFor: (String, String?) -> SceneFactory =
                        { id, _ -> if (id == sceneFactory.sceneId) sceneFactory else sceneFactoryFor?.invoke(id) ?: sceneFactory }
                    val spec =
                        RenderSpec(
                            aspect = aspect,
                            defaults = ExportDefaults(fps = fps, codec = codec, loudnessTargetId = loadExportDefaults().loudnessTargetId),
                            watermark = watermark,
                        )
                    val result =
                        render(
                            project = project,
                            plan = ProjectComposition.plan(project),
                            spec = spec,
                            takes = { exportTake },
                            factoryFor = factoryFor,
                            fileName = name,
                            destination = destination,
                            onProgress = { p, steady ->
                                _exportState.update { it.copy(phase = ExportPhase.Running(p)) }
                                ExportRun.publish(p, steady = steady)
                            },
                            isCancelled = { exportCancelled || ExportRun.cancelRequested },
                        )
                    _exportState.value = oneTapState(result, destination, name)
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
                        val reason = t.message ?: application.getString(dev.geode.R.string.export_error_no_message)
                        val detail = "${t.javaClass.simpleName}: $reason"
                        _exportState.value = ExportUiState(phase = ExportPhase.Failed(detail))
                    }
                } finally {
                    ExportRun.finish()
                }
            }
    }

    private fun oneTapState(
        result: RenderResult,
        destination: Uri?,
        name: String,
    ): ExportUiState =
        when (result) {
            is RenderResult.Saved ->
                ExportUiState(
                    customDestination = destination != null,
                    phase = ExportPhase.Done(result.uri),
                    loudnessAdvice = result.loudnessAdvice,
                    fileName = name.takeIf { destination == null },
                )
            is RenderResult.Failed -> ExportUiState(customDestination = destination != null, phase = ExportPhase.Failed(result.message))
            RenderResult.Cancelled -> ExportUiState(customDestination = destination != null)
            // The style that renders is checked, not only the one on stage when Render was pressed:
            // a take records whatever was on stage, a previewed Pro style or one used during the
            // trial included, and exporting it must not hand that style out for free.
            RenderResult.Locked -> {
                host.onStyleLocked()
                ExportUiState()
            }
            RenderResult.NothingToRender ->
                ExportUiState(phase = ExportPhase.Failed(application.getString(dev.geode.R.string.editor_export_nothing)))
        }

    /**
     * The project a one-tap render is: the song (or the chosen [range] of it) on an audio lane, and
     * one visual clip over exactly that span. With a [take] the clip is the take, its source time
     * set so take time lines up with the song as it was recorded. Without a [range] the song's own
     * length is not needed: a clip running the whole source from its start renders the whole song.
     */
    private fun oneTapProject(
        song: Uri,
        range: ExportRange?,
        sceneId: String,
        milkPath: String?,
        take: PerformanceTake.Timeline?,
    ): EditorProject {
        val startMs = range?.startMs ?: 0L
        val lengthMs = range?.durationMs ?: WHOLE_SONG_MS
        val audio =
            Clip(
                id = ClipId("song"),
                content = ClipContent.Audio(song.toString()),
                startMs = 0L,
                durationMs = lengthMs,
                sourceInMs = startMs,
                sourceDurationMs = if (range == null) lengthMs else 0L,
            )
        val visual =
            Clip(
                id = ClipId("visual"),
                content = ClipContent.Scene(sceneId = sceneId, milkPath = milkPath, takeId = take?.let { ONE_TAP_TAKE }),
                startMs = 0L,
                durationMs = lengthMs,
                sourceInMs = take?.let { startMs - it.trackOffsetMs } ?: 0L,
            )
        return EditorProject(
            timeline =
                Timeline(
                    lanes =
                        listOf(
                            Lane(LaneId("visual"), LaneKind.Visual, clips = listOf(visual)),
                            Lane(LaneId("song"), LaneKind.Audio, clips = listOf(audio)),
                        ),
                    durationMs = lengthMs,
                ),
        )
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
        // The scope picker hides batches below Android 10; this catches any other way in, since
        // there every track would fail at the MediaStore insert (RenderScope.BATCH_MIN_SDK).
        if (!RenderScope.batchSupported(Build.VERSION.SDK_INT)) {
            _exportState.value =
                ExportUiState(phase = ExportPhase.Failed(application.getString(dev.geode.R.string.render_batch_needs_q)))
            return
        }
        exportCancelled = false
        var batch = RenderBatch(tracks)
        _exportState.value = ExportUiState(phase = ExportPhase.Running(0f), batch = batch)
        ExportRun.begin(application.getString(dev.geode.R.string.render_batch_label, tracks.size))
        ExportService.start(application)
        exportJob =
            ExportRun.launch {
                try {
                    val gui = host.guiPrefs
                    // A Cancel between tracks ends the loop; the tracks left are then marked cancelled.
                    var track = batch.current
                    while (track != null && !exportCancelled && !ExportRun.cancelRequested) {
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
                isCancelled = { exportCancelled || ExportRun.cancelRequested },
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

    /**
     * Stops the render through [ExportRun], not only through this controller's flag: after the
     * Activity is recreated this controller is a new one that re-attached to a render an older one
     * started, whose `isCancelled` never reads [exportCancelled]. [ExportRun.requestCancel] reaches
     * that render whichever controller asks.
     */
    fun cancelExport() {
        exportCancelled = true
        if (ExportRun.running) ExportRun.requestCancel()
    }

    fun resetExportState() {
        if (!_exportState.value.phase.isBusy) _exportState.value = ExportUiState()
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

    /**
     * One clip of the old single-clip editor, re-encoded with its edit. It runs under [ExportRun]
     * and [ExportService] like every other render, so leaving the app does not end it.
     */
    fun startStudioExport(
        clip: dev.geode.export.StudioClip,
        edit: dev.geode.export.ClipEdit,
        destination: Uri? = null,
    ) {
        if (_studio.value.phase.isBusy || ExportRun.running) return
        studioCancelled = false
        _studio.update { it.copy(phase = ExportPhase.Running(0f), notice = null) }
        ExportRun.begin(application.getString(dev.geode.R.string.render_video_label))
        ExportService.start(application)
        studioJob =
            ExportRun.launch {
                runStudioRender {
                    val name = "muzviz_studio_${System.currentTimeMillis()}.mp4"
                    val result =
                        studioExporter.export(
                            source = Uri.parse(clip.uri),
                            sourceDurationMs = clip.durationMs,
                            edit = edit,
                            displayName = name,
                            codec = loadExportDefaults().codec,
                            destination = destination,
                            isCancelled = ::studioCancelRequested,
                        ) { p -> publishStudioProgress(p) }
                    _studio.update { it.copy(phase = result.toPhase()) }
                }
            }
    }

    /**
     * Renders a Studio project: its edit, encoded with its own export settings, saved to its own
     * save target unless [destination] names a file. This is the one entry point every Studio
     * render goes through (Render, a stopped recording, a batch of projects from a template); see
     * [render] for how each clip is drawn. [onRendered] gets the finished video for the project's
     * list ([dev.geode.ui.EditorController.recordRender]), on the main thread, where edits run.
     */
    fun renderProject(
        project: StudioProject,
        destination: Uri? = null,
        watermark: Boolean = false,
        onRendered: (ProjectRender) -> Unit = {},
    ) {
        if (_studio.value.phase.isBusy || ExportRun.running) return
        studioCancelled = false
        _studio.update { it.copy(phase = ExportPhase.Running(0f), notice = null) }
        ExportRun.begin(project.name.ifBlank { application.getString(dev.geode.R.string.render_video_label) })
        ExportService.start(application)
        studioJob =
            ExportRun.launch {
                runStudioRender {
                    val export = project.export
                    val plan = ProjectComposition.plan(project.edit)
                    val notice = plan.report.message(application)
                    val name = "muzviz_cut_${System.currentTimeMillis()}.mp4"
                    val target = destination ?: withContext(Dispatchers.IO) { folderDocument(export.saveTo, name) }
                    val result =
                        render(
                            project = project.edit,
                            plan = plan,
                            spec = RenderSpec(ExportAspect.of(export.quality, export.ratio), export, watermark),
                            takes = { takeId -> loadTake(project.id, takeId) },
                            factoryFor = ::plainSceneFactory,
                            fileName = name,
                            destination = target,
                            onProgress = { p, _ -> publishStudioProgress(p) },
                            isCancelled = ::studioCancelRequested,
                        )
                    val phase =
                        when (result) {
                            is RenderResult.Saved -> {
                                val label = project.name.ifBlank { name }
                                val render = ProjectRender(result.uri.toString(), label, System.currentTimeMillis(), result.durationMs)
                                withContext(Dispatchers.Main) { onRendered(render) }
                                ExportPhase.Done(result.uri)
                            }
                            is RenderResult.Failed -> ExportPhase.Failed(result.message)
                            RenderResult.Cancelled -> ExportPhase.Idle
                            RenderResult.Locked -> {
                                host.onStyleLocked()
                                ExportPhase.Idle
                            }
                            RenderResult.NothingToRender ->
                                ExportPhase.Failed(application.getString(dev.geode.R.string.editor_export_nothing))
                        }
                    _studio.update { it.copy(phase = phase, notice = notice) }
                    // The report reaches the user whatever page they are on: the render may finish
                    // long after they left Studio.
                    if (notice != null && result != RenderResult.Cancelled) {
                        withContext(Dispatchers.Main) { Toast.makeText(application, notice, Toast.LENGTH_LONG).show() }
                    }
                }
            }
    }

    /** The shared ending of a Studio render: failures to [ExportPhase.Failed], the run released. */
    private suspend fun runStudioRender(block: suspend () -> Unit) {
        try {
            block()
            refreshStudioClips()
        } catch (c: kotlinx.coroutines.CancellationException) {
            _studio.update { it.copy(phase = ExportPhase.Idle) }
            throw c
        } catch (t: Throwable) {
            val phase =
                if (studioCancelled) {
                    ExportPhase.Idle
                } else {
                    val reason = t.message ?: application.getString(dev.geode.R.string.export_error_no_message)
                    ExportPhase.Failed("${t.javaClass.simpleName}: $reason")
                }
            _studio.update { it.copy(phase = phase) }
        } finally {
            studioJob = null
            ExportRun.finish()
        }
    }

    private fun studioCancelRequested(): Boolean = studioCancelled || ExportRun.cancelRequested

    private fun publishStudioProgress(p: Float) {
        val clamped = p.coerceIn(0f, 1f)
        _studio.update { it.copy(phase = ExportPhase.Running(clamped)) }
        ExportRun.publish(clamped)
    }

    /** How a render is encoded: a project's export settings, or the one-tap render's own choices. */
    private class RenderSpec(
        val aspect: ExportAspect,
        val defaults: ExportDefaults,
        val watermark: Boolean,
    )

    private sealed interface RenderResult {
        data class Saved(
            val uri: Uri,
            val durationMs: Long,
            val loudnessAdvice: LoudnessAdvice? = null,
        ) : RenderResult

        data class Failed(
            val message: String,
        ) : RenderResult

        data object Cancelled : RenderResult

        /** A style the project draws is Pro and the entitlement does not allow it. */
        data object Locked : RenderResult

        data object NothingToRender : RenderResult
    }

    /** What one scene segment draws: its style, and the look over time (a take, keyframes) if it moves. */
    private class SegmentLook(
        val factory: SceneFactory,
        val paramsAt: ((Long) -> SceneParams)?,
        /** The styles the segment switches to (a take's switches, a beat cut), timed natively. */
        val styleSchedule: StyleSchedule = StyleSchedule.NONE,
    )

    /**
     * The one render path (#232). Every render is a [project] and its [plan]:
     * - a direct plan (one visual over one stretch of song) is drawn by the visualizer exporter
     *   straight into the destination;
     * - any other plan draws each scene segment offscreen into a `studio-` scratch MP4 (swept by
     *   GeodeContainer if the process dies), then [ProjectComposition.compose] layers them with the
     *   footage and the audio lanes and Transformer writes the file.
     *
     * Every style the project draws is checked against the entitlement: each segment's opening style
     * here, before anything renders ([RenderResult.Locked]), and every later switch (a take's style
     * changes, a beat cut's list) by the native schedule, which drops the locked ones (#357).
     */
    @Suppress("LongParameterList")
    private suspend fun render(
        project: EditorProject,
        plan: ProjectComposition.RenderPlan,
        spec: RenderSpec,
        takes: suspend (String) -> PerformanceTake.Timeline?,
        factoryFor: (String, String?) -> SceneFactory,
        fileName: String,
        destination: Uri?,
        onProgress: (Float, Boolean) -> Unit,
        isCancelled: () -> Boolean,
    ): RenderResult {
        val segments = plan.segments
        if (segments.isEmpty() && plan.clips.none { it is ProjectComposition.ClipRender.Footage }) return RenderResult.NothingToRender
        val looks = segments.associateWith { lookFor(it, project.keyframes, it.scene.takeId?.let { id -> takes(id) }, factoryFor) }
        if (looks.values.any { !host.allowsStyle(it.factory.sceneId) }) return RenderResult.Locked

        plan.direct?.let { segment ->
            val result = drawSegment(segment, looks.getValue(segment), spec, destination, fileName, final = true, onProgress, isCancelled)
            return when (result) {
                is VideoExporter.Result.Saved -> RenderResult.Saved(result.uri, segment.durationMs, result.loudnessAdvice)
                is VideoExporter.Result.Failed -> RenderResult.Failed(result.message)
                VideoExporter.Result.Cancelled -> RenderResult.Cancelled
            }
        }

        val scratch = mutableListOf<File>()
        try {
            val drawnShare = if (segments.isEmpty()) 0f else DRAW_SPAN
            val totalMs = segments.sumOf(SceneSegment::durationMs).coerceAtLeast(1L).toFloat()
            var doneMs = 0L
            val drawn = LinkedHashMap<SceneSegment, Uri>()
            for (segment in segments) {
                val file = File(application.cacheDir, "studio-scene-${System.currentTimeMillis()}-${scratch.size}.mp4")
                scratch += file
                val before = doneMs
                val progress: (Float, Boolean) -> Unit =
                    { p, _ -> onProgress(drawnShare * (before + p * segment.durationMs) / totalMs, true) }
                val look = looks.getValue(segment)
                when (val result = drawSegment(segment, look, spec, Uri.fromFile(file), "", final = false, progress, isCancelled)) {
                    is VideoExporter.Result.Saved -> drawn[segment] = Uri.fromFile(file)
                    is VideoExporter.Result.Failed -> return RenderResult.Failed(result.message)
                    VideoExporter.Result.Cancelled -> return RenderResult.Cancelled
                }
                doneMs += segment.durationMs
            }
            val composed = withContext(Dispatchers.IO) { ProjectComposition.compose(application, project, plan, drawn, spec.defaults) }
            if (composed !is ProjectComposition.Outcome.Ready) return RenderResult.NothingToRender
            val result =
                studioExporter.exportComposition(
                    composed.composition,
                    composed.durationMs,
                    fileName,
                    spec.defaults.codec,
                    destination,
                    isCancelled,
                ) { p -> onProgress(drawnShare + (1f - drawnShare) * p.coerceIn(0f, 1f), true) }
            return when (result) {
                is StudioExporter.Result.Saved -> RenderResult.Saved(result.uri, result.durationMs)
                is StudioExporter.Result.Failed -> RenderResult.Failed(result.message)
                StudioExporter.Result.Cancelled -> RenderResult.Cancelled
            }
        } finally {
            scratch.forEach { it.delete() }
        }
    }

    /**
     * One scene segment through the visualizer exporter (`OffscreenSceneRenderer`), hearing its
     * song. A [final] draw is the whole render, so it gets the codec, loudness readout and file
     * name; a scratch draw is H.264 for Transformer to read back, and is measured by nobody.
     */
    @Suppress("LongParameterList")
    private suspend fun drawSegment(
        segment: SceneSegment,
        look: SegmentLook,
        spec: RenderSpec,
        destination: Uri?,
        fileName: String,
        final: Boolean,
        onProgress: (Float, Boolean) -> Unit,
        isCancelled: () -> Boolean,
    ): VideoExporter.Result {
        val gui = host.guiPrefs
        return exporter.export(
            audioUri = segment.songUri.toUri(),
            sceneFactory = look.factory,
            aspect = spec.aspect,
            fileName = fileName,
            sceneParams = host.sceneParams,
            lfoConfigs = host.lfoConfigs(),
            adsrConfigs = host.adsrConfigs(),
            reactions = host.reactions(),
            reducedMotion = gui.reducedMotion,
            requestedFps = spec.defaults.fps,
            soundTuning = gui.soundTuning,
            paramsAt = look.paramsAt,
            styleSchedule = look.styleSchedule,
            range = segment.range,
            destination = destination,
            codec = if (final) spec.defaults.codec else ExportCodec.H264,
            loudnessTarget = if (final) LoudnessTarget.byId(spec.defaults.loudnessTargetId) else LoudnessTarget.LeaveAsIs,
            background = defaultBackground(),
            onProgress = { p -> onProgress(p, p >= VideoExporter.AUDIO_PROGRESS_SPAN) },
            isCancelled = isCancelled,
            watermark = spec.watermark,
        )
    }

    /**
     * The look a segment draws. A take clip plays its recorded events at take time (the clip's
     * source time), opening on the style in force where the segment starts and switching through
     * every later one (Studio 2D); a beat-cut clip steps its styles on the beat; a look clip is the
     * stage's look. When each switch lands is the native renderer's call (`core/viz/StyleSchedule`):
     * this only hands it the take's switches and the cut's list. The project's
     * scene keyframes, programme-wide or on this clip, fold over either. A segment that neither
     * takes nor keyframes move draws one still look ([SegmentLook.paramsAt] null).
     */
    private fun lookFor(
        segment: SceneSegment,
        sheet: KeyframeSheet,
        take: PerformanceTake.Timeline?,
        factoryFor: (String, String?) -> SceneFactory,
    ): SegmentLook {
        val clip = segment.clip
        val takeStartMs = clip.sourceTimeAt(segment.programmeStartMs)
        val opening = take?.stateAt(takeStartMs)
        val sceneId =
            opening?.sceneId?.takeIf { it.isNotEmpty() }
                ?: if (take != null) exportSceneIdFor(take, segment.scene.sceneId) else segment.scene.sceneId
        val milkPath = opening?.milkPath ?: segment.scene.milkPath
        val animated =
            sheet.tracks.any {
                it.enabled && (it.clipId == null || it.clipId == clip.id) && AnimatableParams.find(it.paramId)?.isScene == true
            }
        val paramsAt: ((Long) -> SceneParams)? =
            if (take == null && !animated) {
                null
            } else {
                { ms: Long ->
                    val programmeMs = segment.programmeStartMs + ms
                    val base = take?.stateAt(clip.sourceTimeAt(programmeMs))?.params ?: host.sceneParams
                    if (animated) AnimatableParams.applyToScene(base, sheet.valuesAt(programmeMs, clip.id)) else base
                }
            }
        val switches =
            when {
                take != null -> takeSchedule(take, takeStartMs, segment.durationMs)
                segment.scene.beatCut != null -> beatCutSchedule(sceneId, segment.scene.beatCut)
                else -> StyleSchedule.NONE
            }
        // Every later style is gated natively against the entitlement (#357); the opening one in render().
        val styleSchedule = if (switches.isEmpty) switches else switches.copy(lockedStyleIds = lockedStyleIds())
        return SegmentLook(factoryFor(sceneId, milkPath), paramsAt, styleSchedule)
    }

    /**
     * A take's switches inside this segment, moved from take time to the segment's render time
     * (the clip plays its take at an offset, [Clip.sourceTimeAt]). The look at the segment's start is
     * the factory's, so only later switches are cues.
     */
    private fun takeSchedule(
        take: PerformanceTake.Timeline,
        takeStartMs: Long,
        durationMs: Long,
    ): StyleSchedule =
        StyleSchedule(
            cues =
                take
                    .styleChanges()
                    .map { change -> StyleSchedule.Cue(change.atMs - takeStartMs, change.sceneId, change.milkPath) }
                    .filter { it.atMs in 1..durationMs },
        )

    /**
     * The entitlement as a value for the native schedule (#357): every catalogue style the gate
     * refuses. Only catalogue styles can be drawn, so this is every style a later switch could show
     * that the user does not own; the native schedule drops them where it is built.
     */
    private fun lockedStyleIds(): List<String> = Catalogue.styles.map { it.id }.filterNot(host::allowsStyle)

    /** A beat cut opens on [sceneId] and steps through its list from there. */
    private fun beatCutSchedule(
        sceneId: String,
        cut: BeatCut,
    ): StyleSchedule {
        val ids = if (cut.sceneIds.firstOrNull() == sceneId) cut.sceneIds else listOf(sceneId) + cut.sceneIds
        return StyleSchedule(beatCutIds = ids, everyBeats = cut.everyBeats)
    }

    /** A take recorded into project [id], or null for a project with no folder or no such take. */
    private suspend fun loadTake(
        id: ProjectId,
        takeId: String,
    ): PerformanceTake.Timeline? {
        if (id.value.isEmpty()) return null
        return withContext(Dispatchers.IO) {
            projectStore
                .loadTake(id, takeId)
                ?.let { json -> runCatching { PerformanceTake.Timeline(json) }.getOrNull() }
                ?.takeUnless { it.isEmpty }
        }
    }

    /** A Studio render's style: just the id, and the MilkDrop preset the clip names (if any). */
    private fun plainSceneFactory(
        sceneId: String,
        milkPath: String?,
    ): SceneFactory =
        object : SceneFactory {
            override val sceneId: String = sceneId
            override val milkPresetPath: String? = milkPath
        }

    /**
     * A new document in the project's chosen folder, or null to save in the Videos library (no
     * folder chosen, or the folder's grant is gone; the library needs no permission).
     */
    private fun folderDocument(
        saveTo: SaveTarget,
        name: String,
    ): Uri? {
        val tree = (saveTo as? SaveTarget.Folder)?.treeUri?.toUri() ?: return null
        return runCatching {
            val parent = DocumentsContract.buildDocumentUriUsingTree(tree, DocumentsContract.getTreeDocumentId(tree))
            DocumentsContract.createDocument(application.contentResolver, parent, "video/mp4", name)
        }.getOrElse { e ->
            RingLog.note(TAG, "save folder unavailable, saving to Videos", e)
            null
        }
    }

    private suspend fun loadExportDefaults(): ExportDefaults =
        withContext(Dispatchers.IO) { ExportPrefsStore(GeodePrefsFiles(application).general).load() }

    // The main export's UI (Studio › Render) has no path to this controller's public API for
    // per-render options that aren't already threaded through startExport's callers, so the
    // loudness target rides along as a persisted default instead.
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

    /**
     * Stops a Studio render. Through [ExportRun] as well as the exporter: the render runs there,
     * and after the Activity is recreated it may belong to an older controller (see [cancelExport]).
     */
    fun cancelStudioExport() {
        studioCancelled = true
        studioExporter.cancel()
        if (studioJob != null && ExportRun.running) ExportRun.requestCancel()
        _studio.update { it.copy(phase = ExportPhase.Idle) }
    }

    fun clearStudioResult() {
        _studio.update { it.copy(phase = ExportPhase.Idle, notice = null) }
    }

    private val loopRenderer = LoopRender(application)
    private val loopExtender = LoopExtend(application)

    private val _loopState = MutableStateFlow(LoopUiState())
    val loopState: StateFlow<LoopUiState> = _loopState

    @Volatile
    private var loopCancelled = false

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
        loopCancelled = false
        _loopState.value = LoopUiState(phase = ExportPhase.Running(0f))
        ExportRun.begin(uri.lastPathSegment.orEmpty().substringAfterLast('/'))
        ExportService.start(application)
        loopJob =
            ExportRun.launch {
                try {
                    val gui = host.guiPrefs
                    val spec = LoopSpec.of(loopMs, crossfadeMs, fps = fps)
                    // Plus one loop: the extend rounds the repeats up to a whole loop.
                    val longFormMs = loopRenderer.soundtrackMs(audioClips.ifEmpty { listOf(uri) }) + loopMs
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
                            longFormMs = longFormMs,
                            onProgress = { p -> publishLoopProgress(p * RENDER_SPAN) },
                            isCancelled = { loopCancelled || ExportRun.cancelRequested },
                            watermark = watermark,
                        )
                    _loopState.value = LoopUiState(phase = finishLoopRender(renderResult, uri, audioClips, destination))
                } catch (t: Throwable) {
                    if (t is kotlinx.coroutines.CancellationException) {
                        _loopState.value = LoopUiState()
                        throw t
                    } else if (loopCancelled) {
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

    /** Same reasoning as [cancelExport]: the loop render may belong to an earlier controller. */
    fun cancelLoopRender() {
        loopCancelled = true
        if (ExportRun.running) ExportRun.requestCancel()
    }

    fun clearLoopResult() {
        if (!_loopState.value.phase.isBusy) _loopState.value = LoopUiState()
    }

    private companion object {
        const val TAG = "ExportController"

        /** Share of a composed render's bar spent drawing scenes; the Transformer pass gets the rest. */
        const val DRAW_SPAN = 0.8f

        /**
         * The length a one-tap render's project states when it renders the whole song: the song's
         * own length is read by the exporter, so this only has to say "to the end".
         */
        const val WHOLE_SONG_MS = 24L * 60L * 60L * 1000L

        /** The one-tap render's take clip names the export take by this id. */
        const val ONE_TAP_TAKE = "export-take"

        // Extending mostly copies already-encoded samples, so it gets less of the bar than the
        // GPU render does.
        const val RENDER_SPAN = 0.7f
        const val EXTEND_SPAN = 1f - RENDER_SPAN
    }
}
