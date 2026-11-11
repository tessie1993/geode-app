package dev.geode.ui

import android.app.Application
import android.net.Uri
import android.provider.DocumentsContract
import android.widget.Toast
import androidx.core.net.toUri
import dev.geode.RingLog
import dev.geode.data.BackgroundPrefsStore
import dev.geode.data.ExportDefaults
import dev.geode.data.ExportPrefsStore
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
import dev.geode.editor.ProjectRender
import dev.geode.editor.StudioProject
import dev.geode.editor.Timeline
import dev.geode.editor.cappedAt
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
import dev.geode.export.StudioExporter
import dev.geode.export.TimeOfDayDrift
import dev.geode.export.VideoExporter
import dev.geode.geodeContainer
import dev.geode.model.WatermarkChoice
import dev.geode.render.SceneFactory
import dev.geode.render.bridge.StyleSchedule
import dev.geode.render.look.LookSnapshot
import dev.geode.render.look.LookSource
import dev.geode.render.scene.Catalogue
import dev.geode.render.scene.SceneParams
import dev.geode.util.bestEffort
import dev.geode.viz.BackgroundExportSpec
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.takeWhile
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File

data class StudioUiState(
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
     * The finished file's name in Movies/BeatRealms, so the Render bar can say what was made. Null for a
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

@androidx.annotation.OptIn(androidx.media3.common.util.UnstableApi::class)
internal class ExportController(
    private val application: Application,
    private val scope: CoroutineScope,
    private val host: Host,
) {
    interface Host {
        val exportUri: Uri?

        val sceneId: String

        /** The stage's look; every render pins it once when it starts ([LookSource.pin]), so live edits never reach it. */
        val look: LookSource

        /** Whether the entitlement allows rendering [sceneId] (FeatureGate.allowsStyle). */
        fun allowsStyle(sceneId: String): Boolean

        /** Offers Pro for a locked style; safe from any thread (the paywall request is a StateFlow). */
        fun onStyleLocked()

        /**
         * The entitlement's export limits right now (FeatureGate.renderLimits). [renderProject] reads
         * them itself rather than trusting its caller, so a take's background render is capped and
         * watermarked like the one-tap Render (#354).
         */
        fun renderLimits(): dev.geode.pro.RenderLimits
    }

    private val exporter = VideoExporter(application)
    private val studioExporter = dev.geode.export.StudioExporter(application)

    private val _exportState = MutableStateFlow(ExportUiState())
    val exportState: StateFlow<ExportUiState> = _exportState

    private val studioState = MutableStateFlow(StudioUiState())

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

    /**
     * The one-tap Render: the current track as a project with one visual lane over the song,
     * rendered through the same path as
     * every Studio project ([render]). That project is always a direct one, so the visualizer
     * exporter writes the file itself, as it always has.
     */
    fun startExport(
        aspect: ExportAspect,
        fps: Int,
        sceneFactory: SceneFactory,
        destination: Uri? = null,
        range: ExportRange? = null,
        codec: ExportCodec = ExportCodec.H264,
        watermark: WatermarkChoice = WatermarkChoice.NONE,
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
                    val name = "beatrealms_${System.currentTimeMillis()}.mp4"
                    val project = oneTapProject(uri, range, sceneFactory.sceneId, sceneFactory.milkPresetPath)
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
                            factoryFor = { _, _ -> sceneFactory },
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
                        _exportState.value = ExportUiState(phase = stoppedPhase(application))
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
            RenderResult.Cancelled -> ExportUiState(customDestination = destination != null, phase = stoppedPhase(application))
            // The style that renders is checked, not only the one on stage when Render was pressed.
            RenderResult.Locked -> {
                host.onStyleLocked()
                ExportUiState()
            }
            RenderResult.NothingToRender ->
                ExportUiState(phase = ExportPhase.Failed(application.getString(dev.geode.R.string.editor_export_nothing)))
        }

    /**
     * The project a one-tap render is: the song (or the chosen [range] of it) on an audio lane, and
     * one visual clip over exactly that span. Without a [range] the song's own
     * length is not needed: a clip running the whole source from its start renders the whole song.
     */
    private fun oneTapProject(
        song: Uri,
        range: ExportRange?,
        sceneId: String,
        milkPath: String?,
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
                content = ClipContent.Scene(sceneId = sceneId, milkPath = milkPath),
                startMs = 0L,
                durationMs = lengthMs,
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
     * own `beatrealms_*.mp4` in Movies/BeatRealms, under one [ExportRun] so the foreground service and its
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
        watermark: WatermarkChoice,
    ) {
        if (tracks.isEmpty() || _exportState.value.phase.isBusy || ExportRun.running) return
        exportCancelled = false
        var batch = RenderBatch(tracks)
        _exportState.value = ExportUiState(phase = ExportPhase.Running(0f), batch = batch)
        ExportRun.begin(application.getString(dev.geode.R.string.render_batch_label, tracks.size))
        ExportService.start(application)
        exportJob =
            ExportRun.launch {
                try {
                    val look = host.look.pin().let { live -> sceneParams?.let { live.copy(params = it) } ?: live }
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
                                renderBatchTrack(track, aspect, fps, sceneFactory, look, codec, watermark, publish)
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
        look: LookSnapshot,
        codec: ExportCodec,
        watermark: WatermarkChoice,
        publish: (Float) -> Unit,
    ): BatchOutcome {
        val uri = track.uri.toUri()
        val name = "beatrealms_${System.currentTimeMillis()}.mp4"
        val result =
            exporter.export(
                audioUri = uri,
                sceneFactory = sceneFactory,
                aspect = aspect,
                fileName = name,
                look = look,
                requestedFps = fps,
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
                    else -> stoppedPhase(application)
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

    /** Deletes a finished render's file (My videos' delete); [onResult] says whether it went. */
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
            onResult(ok)
        }
    }

    /**
     * Renders a Studio project: its edit, encoded with its own export settings, saved to its own
     * save target unless [destination] names a file. This is the one entry point every Studio
     * render goes through (Render, a batch of projects from a template); see
     * [render] for how each clip is drawn. [onRendered] gets the finished video for the project's
     * list ([dev.geode.ui.EditorController.recordRender]), on the main thread, where edits run.
     *
     * The entitlement's limits ([Host.renderLimits]) apply here, to what is drawn: past the cap the
     * edit is cut ([cappedAt]) and the wordmark goes on when the gate says so. Pro renders are
     * untouched; a trial or free render is the same first minute, watermarked, that the one-tap
     * Render gives.
     */
    fun renderProject(
        project: StudioProject,
        destination: Uri? = null,
        onRendered: (ProjectRender) -> Unit = {},
    ) {
        if (studioState.value.phase.isBusy || ExportRun.running) return
        val limits = host.renderLimits()
        val edit = project.cappedAt(limits.capMs).edit
        studioState.update { it.copy(phase = ExportPhase.Running(0f), notice = null) }
        ExportRun.begin(project.name.ifBlank { application.getString(dev.geode.R.string.render_video_label) })
        ExportService.start(application)
        studioJob =
            ExportRun.launch {
                runStudioRender {
                    val export = project.export
                    val plan = ProjectComposition.plan(edit)
                    val notice = plan.report.message(application)
                    val name = "beatrealms_cut_${System.currentTimeMillis()}.mp4"
                    // A document this call makes in the user's folder exists the moment it is created,
                    // so every outcome but Saved deletes it again (#366): a locked style, nothing to
                    // render, a failed segment, Cancel, an exception or the job's own cancellation. A
                    // destination the caller passed in is theirs and is left alone.
                    val created =
                        if (destination == null) withContext(Dispatchers.IO) { folderDocument(export.saveTo, name) } else null
                    val target = destination ?: created
                    var saved = false
                    try {
                        val result =
                            render(
                                project = edit,
                                plan = plan,
                                spec = RenderSpec(ExportAspect.of(export.quality, export.ratio), export, limits.watermark),
                                factoryFor = ::plainSceneFactory,
                                fileName = name,
                                destination = target,
                                onProgress = { p, _ -> publishStudioProgress(p) },
                                isCancelled = ::studioCancelRequested,
                            )
                        saved = result is RenderResult.Saved
                        val phase =
                            when (result) {
                                is RenderResult.Saved -> {
                                    val label = project.name.ifBlank { name }
                                    val render = ProjectRender(result.uri.toString(), label, System.currentTimeMillis(), result.durationMs)
                                    withContext(Dispatchers.Main) { onRendered(render) }
                                    ExportPhase.Done(result.uri)
                                }
                                is RenderResult.Failed -> ExportPhase.Failed(result.message)
                                RenderResult.Cancelled -> stoppedPhase(application)
                                RenderResult.Locked -> {
                                    host.onStyleLocked()
                                    ExportPhase.Idle
                                }
                                RenderResult.NothingToRender ->
                                    ExportPhase.Failed(application.getString(dev.geode.R.string.editor_export_nothing))
                            }
                        studioState.update { it.copy(phase = phase, notice = notice) }
                        // The report reaches the user whatever page they are on: the render may finish
                        // long after they left Studio.
                        if (notice != null && result != RenderResult.Cancelled) {
                            withContext(Dispatchers.Main) { Toast.makeText(application, notice, Toast.LENGTH_LONG).show() }
                        }
                    } finally {
                        if (!saved && created != null) discardUnusedTarget(created)
                    }
                }
            }
    }

    /**
     * Deletes a render target this controller created that never received a video. NonCancellable is
     * the only element of the outer context, with IO inside it, so the delete still runs when the
     * render was cancelled and leaving it cannot throw a fresh CancellationException over the original
     * outcome (kotlinx.coroutines, NonCancellable pitfalls). The direct and Studio exporters may already
     * have deleted it on their own failure paths, so a second delete failing is expected and quiet.
     */
    private suspend fun discardUnusedTarget(target: Uri) {
        withContext(NonCancellable) {
            withContext(Dispatchers.IO) {
                bestEffort(TAG, "delete unused render target") { DocumentsContract.deleteDocument(application.contentResolver, target) }
            }
        }
    }

    /** The shared ending of a Studio render: failures to [ExportPhase.Failed], the run released. */
    private suspend fun runStudioRender(block: suspend () -> Unit) {
        try {
            block()
        } catch (c: kotlinx.coroutines.CancellationException) {
            studioState.update { it.copy(phase = stoppedPhase(application)) }
            throw c
        } catch (t: Throwable) {
            val phase =
                if (ExportRun.cancelRequested) {
                    stoppedPhase(application)
                } else {
                    val reason = t.message ?: application.getString(dev.geode.R.string.export_error_no_message)
                    ExportPhase.Failed("${t.javaClass.simpleName}: $reason")
                }
            studioState.update { it.copy(phase = phase) }
        } finally {
            studioJob = null
            ExportRun.finish()
        }
    }

    private fun studioCancelRequested(): Boolean = ExportRun.cancelRequested

    private fun publishStudioProgress(p: Float) {
        val clamped = p.coerceIn(0f, 1f)
        studioState.update { it.copy(phase = ExportPhase.Running(clamped)) }
        ExportRun.publish(clamped)
    }

    /** How a render is encoded: a project's export settings, or the one-tap render's own choices. */
    private class RenderSpec(
        val aspect: ExportAspect,
        val defaults: ExportDefaults,
        val watermark: WatermarkChoice,
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

    /** What one scene segment draws: its style, and the look over time (keyframes) if it moves. */
    private class SegmentLook(
        val factory: SceneFactory,
        val paramsAt: ((Long) -> SceneParams)?,
        /** The styles the segment switches to (a beat cut), timed natively. */
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
     * here, before anything renders ([RenderResult.Locked]), and every later switch (a beat cut's
     * list) by the native schedule, which drops the locked ones (#357).
     */
    @Suppress("LongParameterList")
    private suspend fun render(
        project: EditorProject,
        plan: ProjectComposition.RenderPlan,
        spec: RenderSpec,
        factoryFor: (String, String?) -> SceneFactory,
        fileName: String,
        destination: Uri?,
        onProgress: (Float, Boolean) -> Unit,
        isCancelled: () -> Boolean,
    ): RenderResult {
        val segments = plan.segments
        if (segments.isEmpty() && plan.clips.none { it is ProjectComposition.ClipRender.Footage }) return RenderResult.NothingToRender
        val pinned = host.look.pin()
        val looks =
            segments.associateWith { lookFor(it, project.keyframes, factoryFor, pinned) }
        if (looks.values.any { !host.allowsStyle(it.factory.sceneId) }) return RenderResult.Locked

        plan.direct?.let { segment ->
            val result =
                drawSegment(segment, looks.getValue(segment), pinned, spec, destination, fileName, final = true, onProgress, isCancelled)
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
                when (val result = drawSegment(segment, look, pinned, spec, Uri.fromFile(file), "", final = false, progress, isCancelled)) {
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
        pinned: LookSnapshot,
        spec: RenderSpec,
        destination: Uri?,
        fileName: String,
        final: Boolean,
        onProgress: (Float, Boolean) -> Unit,
        isCancelled: () -> Boolean,
    ): VideoExporter.Result =
        exporter.export(
            audioUri = segment.songUri.toUri(),
            sceneFactory = look.factory,
            aspect = spec.aspect,
            fileName = fileName,
            look = pinned,
            requestedFps = spec.defaults.fps,
            paramsAt = look.paramsAt,
            styleSchedule = look.styleSchedule,
            range = segment.range,
            destination = destination,
            codec = if (final) spec.defaults.codec else ExportCodec.H264,
            loudnessTarget = if (final) LoudnessTarget.byId(spec.defaults.loudnessTargetId) else LoudnessTarget.LeaveAsIs,
            background = defaultBackground(),
            onProgress = { p -> onProgress(p, p >= VideoExporter.AUDIO_PROGRESS_SPAN) },
            isCancelled = isCancelled,
            watermark = if (final) spec.watermark else spec.watermark.copy(use = WatermarkChoice.USE_SEGMENT),
        )

    /**
     * The look a segment draws: a beat-cut clip steps its styles on the beat (when each switch lands
     * is the native renderer's call, `core/viz/StyleSchedule`); a look clip is the stage's look. The
     * project's scene keyframes, programme-wide or on this clip, fold over either. A segment no
     * keyframe moves draws one still look ([SegmentLook.paramsAt] null).
     */
    private fun lookFor(
        segment: SceneSegment,
        sheet: KeyframeSheet,
        factoryFor: (String, String?) -> SceneFactory,
        pinned: LookSnapshot,
    ): SegmentLook {
        val clip = segment.clip
        val sceneId = segment.scene.sceneId
        val milkPath = segment.scene.milkPath
        val animated =
            sheet.tracks.any {
                it.enabled && (it.clipId == null || it.clipId == clip.id) && AnimatableParams.find(it.paramId)?.isScene == true
            }
        val paramsAt: ((Long) -> SceneParams)? =
            if (!animated) {
                null
            } else {
                { ms: Long ->
                    val programmeMs = segment.programmeStartMs + ms
                    AnimatableParams.applyToScene(pinned.params, sheet.valuesAt(programmeMs, clip.id))
                }
            }
        val switches = segment.scene.beatCut?.let { beatCutSchedule(sceneId, it) } ?: StyleSchedule.NONE
        // Every later style is gated natively against the entitlement (#357); the opening one in render().
        val styleSchedule = if (switches.isEmpty) switches else switches.copy(lockedStyleIds = lockedStyleIds())
        return SegmentLook(factoryFor(sceneId, milkPath), paramsAt, styleSchedule)
    }

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
        withContext(Dispatchers.IO) { ExportPrefsStore(application.geodeContainer.prefsFiles.general).load() }

    // The main export's UI (Studio › Render) has no path to this controller's public API for
    // per-render options that aren't already threaded through startExport's callers, so the
    // loudness target rides along as a persisted default instead.
    private fun defaultLoudnessTarget(): LoudnessTarget =
        LoudnessTarget.byId(
            ExportPrefsStore(application.geodeContainer.prefsFiles.general).load().loudnessTargetId,
        )

    // Same "no per-render option wired through startExport's callers" situation as
    // defaultLoudnessTarget() above: the background image rides along as the persisted default.
    private fun defaultBackground(): BackgroundExportSpec? {
        val p = BackgroundPrefsStore(application.geodeContainer.prefsFiles.background).load()
        val uri = p.uri ?: return null
        return BackgroundExportSpec(uri.toUri(), p.blend, p.amount, p.blurRadius, p.dim)
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
        watermark: WatermarkChoice = WatermarkChoice.NONE,
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
                    val look = host.look.pin()
                    val spec = LoopSpec.of(loopMs, crossfadeMs, fps = fps)
                    // Plus one loop: the extend rounds the repeats up to a whole loop.
                    val longFormMs = loopRenderer.soundtrackMs(audioClips.ifEmpty { listOf(uri) }) + loopMs
                    val renderResult =
                        loopRenderer.render(
                            sceneFactory = sceneFactory,
                            aspect = aspect,
                            spec = spec,
                            look = look,
                            drift = drift,
                            codec = codec,
                            audioUri = uri,
                            longFormMs = longFormMs,
                            onProgress = { p -> publishLoopProgress(p * RENDER_SPAN) },
                            isCancelled = { loopCancelled || ExportRun.cancelRequested },
                            watermark = watermark,
                        )
                    _loopState.value = LoopUiState(phase = finishLoopRender(renderResult, uri, audioClips, destination))
                } catch (t: Throwable) {
                    if (t is kotlinx.coroutines.CancellationException) {
                        _loopState.value = LoopUiState(phase = stoppedPhase(application))
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
            LoopRender.Result.Cancelled -> stoppedPhase(application)
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
                    val name = "beatrealms_loop_${System.currentTimeMillis()}.mp4"
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
                        LoopExtend.Result.Cancelled -> stoppedPhase(application)
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

        // Extending mostly copies already-encoded samples, so it gets less of the bar than the
        // GPU render does.
        const val RENDER_SPAN = 0.7f
        const val EXTEND_SPAN = 1f - RENDER_SPAN
    }
}
