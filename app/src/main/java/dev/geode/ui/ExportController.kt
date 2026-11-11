package dev.geode.ui

import android.app.Application
import android.net.Uri
import androidx.core.net.toUri
import dev.geode.data.BackgroundPrefsStore
import dev.geode.data.ExportDefaults
import dev.geode.data.ExportPrefsStore
import dev.geode.data.GeodePrefsFiles
import dev.geode.editor.MIN_CLIP_DURATION_MS
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
import dev.geode.export.RenderBatch
import dev.geode.export.TimeOfDayDrift
import dev.geode.export.VideoExporter
import dev.geode.render.SceneFactory
import dev.geode.render.look.LookSnapshot
import dev.geode.render.look.LookSource
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
    }

    private val exporter = VideoExporter(application)

    private val _exportState = MutableStateFlow(ExportUiState())
    val exportState: StateFlow<ExportUiState> = _exportState

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

    /**
     * The one-tap Render: the current track (or [range] of it) under the look on stage, written by
     * the visualizer exporter straight into the destination ([renderOneTap]).
     */
    fun startExport(
        aspect: ExportAspect,
        fps: Int,
        sceneFactory: SceneFactory,
        destination: Uri? = null,
        range: ExportRange? = null,
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
                    val name = "beatrealms_${System.currentTimeMillis()}.mp4"
                    val result =
                        renderOneTap(
                            song = uri,
                            range = range,
                            sceneFactory = sceneFactory,
                            spec =
                                OneTapSpec(
                                    aspect = aspect,
                                    fps = fps,
                                    codec = codec,
                                    loudnessTarget = LoudnessTarget.byId(loadExportDefaults().loudnessTargetId),
                                    watermark = watermark,
                                ),
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
            // The style is checked where the render runs, not only when Render was pressed.
            RenderResult.Locked -> {
                host.onStyleLocked()
                ExportUiState()
            }
            RenderResult.NothingToRender ->
                ExportUiState(phase = ExportPhase.Failed(application.getString(dev.geode.R.string.editor_export_nothing)))
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
        watermark: Boolean,
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
        watermark: Boolean,
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

    /** How a one-tap render is encoded: the choices the Render page passed in. */
    private class OneTapSpec(
        val aspect: ExportAspect,
        val fps: Int,
        val codec: ExportCodec,
        val loudnessTarget: LoudnessTarget,
        val watermark: Boolean,
    )

    private sealed interface RenderResult {
        data class Saved(
            val uri: Uri,
            val loudnessAdvice: LoudnessAdvice? = null,
        ) : RenderResult

        data class Failed(
            val message: String,
        ) : RenderResult

        data object Cancelled : RenderResult

        /** The style on stage is Pro and the entitlement does not allow it. */
        data object Locked : RenderResult

        data object NothingToRender : RenderResult
    }

    /**
     * One visual over one stretch of song, drawn by the visualizer exporter (`OffscreenSceneRenderer`)
     * straight into [destination], hearing [song] from [range]'s start (the whole song when null), with
     * the loudness readout, background image and watermark.
     *
     * This was the direct branch of the Studio project render (#232), which planned every render as a
     * project and composed anything else through Media3 Transformer. The owner dropped video editing
     * (ADR 0007, #227), so the one-tap Render became that render's only caller and its project was
     * always this one: a single scene clip over a single song clip of the same span, which the plan
     * always made direct. The plan, the composition and the Studio exporter went with the editor.
     *
     * The checks keep the plan's order: a range shorter than [MIN_CLIP_DURATION_MS] has no stretch of
     * song to draw over (the plan skipped it), then the style is checked against the entitlement.
     */
    private suspend fun renderOneTap(
        song: Uri,
        range: ExportRange?,
        sceneFactory: SceneFactory,
        spec: OneTapSpec,
        fileName: String,
        destination: Uri?,
        onProgress: (Float, Boolean) -> Unit,
        isCancelled: () -> Boolean,
    ): RenderResult {
        if (range != null && range.durationMs < MIN_CLIP_DURATION_MS) return RenderResult.NothingToRender
        val pinned = host.look.pin()
        if (!host.allowsStyle(sceneFactory.sceneId)) return RenderResult.Locked
        val result =
            exporter.export(
                audioUri = song,
                sceneFactory = sceneFactory,
                aspect = spec.aspect,
                fileName = fileName,
                look = pinned,
                requestedFps = spec.fps,
                range = range,
                destination = destination,
                codec = spec.codec,
                loudnessTarget = spec.loudnessTarget,
                background = defaultBackground(),
                onProgress = { p -> onProgress(p, p >= VideoExporter.AUDIO_PROGRESS_SPAN) },
                isCancelled = isCancelled,
                watermark = spec.watermark,
            )
        return when (result) {
            is VideoExporter.Result.Saved -> RenderResult.Saved(result.uri, result.loudnessAdvice)
            is VideoExporter.Result.Failed -> RenderResult.Failed(result.message)
            VideoExporter.Result.Cancelled -> RenderResult.Cancelled
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
        // Extending mostly copies already-encoded samples, so it gets less of the bar than the
        // GPU render does.
        const val RENDER_SPAN = 0.7f
        const val EXTEND_SPAN = 1f - RENDER_SPAN
    }
}
