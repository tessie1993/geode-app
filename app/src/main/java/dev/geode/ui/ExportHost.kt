package dev.geode.ui

import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.padding
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.listSaver
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.export.ExportAspect
import dev.geode.export.ExportCodec
import dev.geode.export.ExportRange
import dev.geode.export.TimeOfDayDrift
import dev.geode.render.SceneFactory
import dev.geode.render.VisualizerView
import dev.geode.ui.glass.GlassButton
import dev.geode.ui.glass.GlassDialog

private data class PendingExport(
    val aspect: ExportAspect,
    val fps: Int,
    val sceneId: String,
    val loopSafe: Boolean,
    val rangeStartMs: Long,
    val rangeDurationMs: Long,
    val codec: ExportCodec,
) {
    val range: ExportRange? get() = if (rangeDurationMs > 0) ExportRange(rangeStartMs, rangeDurationMs) else null
}

private val PendingExportSaver =
    listSaver<PendingExport?, Any>(
        save = { req ->
            if (req == null) {
                emptyList()
            } else {
                listOf(
                    req.aspect.width,
                    req.aspect.height,
                    req.aspect.bitRate,
                    req.fps,
                    req.sceneId,
                    req.loopSafe,
                    req.rangeStartMs,
                    req.rangeDurationMs,
                    req.codec.name,
                )
            }
        },
        restore = { saved ->
            if (saved.isEmpty()) {
                null
            } else {
                PendingExport(
                    aspect = ExportAspect(saved[0] as Int, saved[1] as Int, saved[2] as Int),
                    fps = saved[3] as Int,
                    sceneId = saved[4] as String,
                    loopSafe = saved[5] as Boolean,
                    rangeStartMs = saved[6] as Long,
                    rangeDurationMs = saved[7] as Long,
                    codec = ExportCodec.valueOf(saved[8] as String),
                )
            }
        },
    )

/** [LoopRenderRequest], flattened to primitives so it survives a configuration change. */
private data class PendingLoopExport(
    val width: Int,
    val height: Int,
    val bitRate: Int,
    val codec: String,
    val fps: Int,
    val loopMs: Long,
    val crossfadeMs: Long,
    val driftHueTurns: Float,
    val driftWarmth: Float,
    val driftStops: Int,
    val audioClips: List<String>,
) {
    companion object {
        fun from(request: LoopRenderRequest): PendingLoopExport =
            PendingLoopExport(
                width = request.aspect.width,
                height = request.aspect.height,
                bitRate = request.aspect.bitRate,
                codec = request.codec.name,
                fps = request.fps,
                loopMs = request.loopMs,
                crossfadeMs = request.crossfadeMs,
                driftHueTurns = request.drift.hueTurns,
                driftWarmth = request.drift.warmth,
                driftStops = request.drift.stops,
                audioClips = request.audioClips.map { it.toString() },
            )
    }
}

private val PendingLoopExportSaver =
    listSaver<PendingLoopExport?, Any>(
        save = { req ->
            if (req == null) {
                emptyList()
            } else {
                listOf(
                    req.width,
                    req.height,
                    req.bitRate,
                    req.codec,
                    req.fps,
                    req.loopMs,
                    req.crossfadeMs,
                    req.driftHueTurns,
                    req.driftWarmth,
                    req.driftStops,
                    req.audioClips.size,
                ) + req.audioClips
            }
        },
        restore = { saved ->
            if (saved.isEmpty()) {
                null
            } else {
                val clipCount = saved[10] as Int
                PendingLoopExport(
                    width = saved[0] as Int,
                    height = saved[1] as Int,
                    bitRate = saved[2] as Int,
                    codec = saved[3] as String,
                    fps = saved[4] as Int,
                    loopMs = saved[5] as Long,
                    crossfadeMs = saved[6] as Long,
                    driftHueTurns = saved[7] as Float,
                    driftWarmth = saved[8] as Float,
                    driftStops = saved[9] as Int,
                    audioClips = (0 until clipCount).map { saved[11 + it] as String },
                )
            }
        },
    )

private val PendingStillAspectSaver =
    listSaver<ExportAspect?, Any>(
        save = { aspect -> if (aspect == null) emptyList() else listOf(aspect.width, aspect.height, aspect.bitRate) },
        restore = { saved -> if (saved.isEmpty()) null else ExportAspect(saved[0] as Int, saved[1] as Int, saved[2] as Int) },
    )

/** Which sheet [ExportHost] is showing: the picker between the two kinds of export, or one of them. */
private enum class ExportEntryMode { Menu, Standard, Loop }

@Composable
fun ExportHost(
    viewModel: PlayerViewModel,
    visualizerView: VisualizerView,
    onDismiss: () -> Unit,
) {
    val studioViewModel: StudioViewModel = geodeViewModel()
    val context = LocalContext.current
    val state by viewModel.uiState.collectAsStateWithLifecycle()
    val viz by viewModel.vizState.collectAsStateWithLifecycle()
    val export by studioViewModel.exportState.collectAsStateWithLifecycle()
    val loop by studioViewModel.loopState.collectAsStateWithLifecycle()

    // On API 33+ a render's progress notification (ExportService, foreground since it must
    // survive the Activity going away) is silently suppressed for anyone who has never granted
    // POST_NOTIFICATIONS — nothing else in the app asks for it unless audio capture is turned on.
    // Ask once, right as the first render starts, with one rationale dialog first; the render
    // proceeds either way, whether the permission is granted, denied, or never asked at all.
    var notificationPermissionAsked by rememberSaveable { mutableStateOf(false) }
    var notificationRationaleVisible by rememberSaveable { mutableStateOf(false) }
    val notificationPermission =
        rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { }
    val requestNotificationPermissionOnce = {
        val alreadyGranted =
            Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU ||
                ContextCompat.checkSelfPermission(context, android.Manifest.permission.POST_NOTIFICATIONS) ==
                PackageManager.PERMISSION_GRANTED
        if (!notificationPermissionAsked && !alreadyGranted) {
            notificationPermissionAsked = true
            notificationRationaleVisible = true
        }
    }

    var pendingExport by rememberSaveable(stateSaver = PendingExportSaver) {
        mutableStateOf<PendingExport?>(null)
    }
    var pendingLoopExport by rememberSaveable(stateSaver = PendingLoopExportSaver) {
        mutableStateOf<PendingLoopExport?>(null)
    }
    var chosenMode by rememberSaveable { mutableStateOf<ExportEntryMode?>(null) }
    val stillPhase by studioViewModel.stillState.collectAsStateWithLifecycle()
    var pendingStillAspect by rememberSaveable(stateSaver = PendingStillAspectSaver) {
        mutableStateOf<ExportAspect?>(null)
    }
    // A render already under way (from before this dialog was last opened) reopens onto its own
    // progress instead of the picker, so leaving and coming back never hides a running export.
    val mode =
        chosenMode ?: when {
            loop.phase.isBusy || loop.phase.resultUriOrNull != null || loop.phase.errorOrNull != null -> ExportEntryMode.Loop
            export.phase.isBusy || export.phase.resultUriOrNull != null || export.phase.errorOrNull != null -> ExportEntryMode.Standard
            else -> ExportEntryMode.Menu
        }

    val sceneFactoryFor: (String, String) -> SceneFactory =
        { requested, fallback ->
            val renderer = visualizerView.visualizerRenderer
            renderer.exportSceneFactory(
                if (requested in renderer.availableSceneIds()) requested else fallback,
            )
        }
    val destinationPicker =
        rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("video/mp4")) { dest ->
            val req = pendingExport
            pendingExport = null
            if (dest != null && req != null) {
                viewModel.startExport(
                    req.aspect,
                    req.fps,
                    visualizerView.visualizerRenderer.exportSceneFactory(req.sceneId),
                    destination = dest,
                    loopSafe = req.loopSafe,
                    range = req.range,
                    sceneFactoryFor = { id -> sceneFactoryFor(id, req.sceneId) },
                    codec = req.codec,
                )
            }
        }
    val stillDestinationPicker =
        rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("image/png")) { dest ->
            val aspect = pendingStillAspect
            pendingStillAspect = null
            if (dest != null && aspect != null) {
                studioViewModel.saveStillFrame(
                    aspect,
                    visualizerView.visualizerRenderer.exportSceneFactory(viz.sceneId),
                    destination = dest,
                )
            }
        }
    val onSaveFrame: (ExportAspect) -> Unit = { aspect ->
        // Scoped storage (Q+) can insert straight into Pictures/Geode; below it a still needs the
        // same SAF folder-picker round trip the video path takes for the same reason — see
        // SettingsDialog's onStartToDestination comment.
        if (android.os.Build.VERSION.SDK_INT < android.os.Build.VERSION_CODES.Q) {
            pendingStillAspect = aspect
            stillDestinationPicker.launch("geode_still_${System.currentTimeMillis()}.png")
        } else {
            studioViewModel.saveStillFrame(aspect, visualizerView.visualizerRenderer.exportSceneFactory(viz.sceneId))
        }
    }
    val loopDestinationPicker =
        rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("video/mp4")) { dest ->
            val req = pendingLoopExport
            pendingLoopExport = null
            if (dest != null && req != null) {
                viewModel.startLoopRender(
                    ExportAspect(req.width, req.height, req.bitRate),
                    ExportCodec.valueOf(req.codec),
                    req.fps,
                    visualizerView.visualizerRenderer.exportSceneFactory(viz.sceneId),
                    req.loopMs,
                    req.crossfadeMs,
                    TimeOfDayDrift(req.driftHueTurns, req.driftWarmth, req.driftStops),
                    req.audioClips.map { Uri.parse(it) },
                    destination = dest,
                )
            }
        }
    val takes by studioViewModel.takeState.collectAsStateWithLifecycle()
    when (mode) {
        ExportEntryMode.Menu ->
            GlassDialog(
                onDismissRequest = onDismiss,
                title = stringResource(R.string.export_loop_menu_title),
                text = stringResource(R.string.export_loop_menu_subtitle),
                actions = {
                    GlassButton(
                        text = stringResource(R.string.export_loop_menu_loop),
                        onClick = { chosenMode = ExportEntryMode.Loop },
                    )
                    GlassButton(
                        text = stringResource(R.string.export_loop_menu_standard),
                        onClick = { chosenMode = ExportEntryMode.Standard },
                        modifier = Modifier.padding(start = 8.dp),
                    )
                },
            )
        ExportEntryMode.Loop ->
            LoopRenderSheet(
                state = loop,
                onStart = { req ->
                    requestNotificationPermissionOnce()
                    if (android.os.Build.VERSION.SDK_INT < android.os.Build.VERSION_CODES.Q) {
                        pendingLoopExport = PendingLoopExport.from(req)
                        loopDestinationPicker.launch("geode_loop_${System.currentTimeMillis()}.mp4")
                    } else {
                        viewModel.startLoopRender(
                            req.aspect,
                            req.codec,
                            req.fps,
                            visualizerView.visualizerRenderer.exportSceneFactory(viz.sceneId),
                            req.loopMs,
                            req.crossfadeMs,
                            req.drift,
                            req.audioClips,
                        )
                    }
                },
                onStartToDestination = { req ->
                    requestNotificationPermissionOnce()
                    pendingLoopExport = PendingLoopExport.from(req)
                    loopDestinationPicker.launch("geode_loop_${System.currentTimeMillis()}.mp4")
                },
                onCancel = studioViewModel::cancelLoopRender,
                onDismiss = {
                    studioViewModel.clearLoopResult()
                    onDismiss()
                },
            )
        ExportEntryMode.Standard ->
            SettingsDialog(
                export = export,
                hasMedia = state.hasMedia,
                takes = takes.takes.map { it.name },
                selectedTake = takes.exportTake,
                onSelectTake = studioViewModel::setExportTake,
                bpm = viz.bpm,
                trackDurationMs = state.durationMs,
                onStart = { aspect, fps, loopSafe, range, codec ->
                    requestNotificationPermissionOnce()
                    // Saving into the Videos library without asking is a scoped-storage privilege,
                    // and scoped storage starts at Q. Below it the same insert needs
                    // WRITE_EXTERNAL_STORAGE - a permission this app does not ask for and should not
                    // start asking for - so the write failed with a SecurityException the user saw
                    // raw. The folder picker is the same save by another route and has always worked
                    // here, so on those versions the primary button opens it.
                    if (android.os.Build.VERSION.SDK_INT < android.os.Build.VERSION_CODES.Q) {
                        pendingExport =
                            PendingExport(
                                aspect,
                                fps,
                                viz.sceneId,
                                loopSafe,
                                range?.startMs ?: 0L,
                                range?.durationMs ?: 0L,
                                codec,
                            )
                        destinationPicker.launch("geode_${System.currentTimeMillis()}.mp4")
                    } else {
                        viewModel.startExport(
                            aspect,
                            fps,
                            visualizerView.visualizerRenderer.exportSceneFactory(viz.sceneId),
                            loopSafe = loopSafe,
                            range = range,
                            sceneFactoryFor = { id -> sceneFactoryFor(id, viz.sceneId) },
                            codec = codec,
                        )
                    }
                },
                onStartToDestination = { aspect, fps, loopSafe, range, codec ->
                    requestNotificationPermissionOnce()
                    pendingExport =
                        PendingExport(
                            aspect,
                            fps,
                            viz.sceneId,
                            loopSafe,
                            range?.startMs ?: 0L,
                            range?.durationMs ?: 0L,
                            codec,
                        )
                    destinationPicker.launch("geode_${System.currentTimeMillis()}.mp4")
                },
                onCancel = studioViewModel::cancelExport,
                onDismiss = {
                    studioViewModel.resetExportState()
                    studioViewModel.resetStillState()
                    onDismiss()
                },
                stillPhase = stillPhase,
                onSaveFrame = onSaveFrame,
            )
    }

    if (notificationRationaleVisible) {
        GlassDialog(
            onDismissRequest = { notificationRationaleVisible = false },
            title = stringResource(R.string.export_notification_permission_title),
            text = stringResource(R.string.export_notification_permission_body),
            actions = {
                GlassButton(
                    text = stringResource(R.string.export_notification_permission_skip),
                    onClick = { notificationRationaleVisible = false },
                )
                GlassButton(
                    text = stringResource(R.string.action_ok),
                    onClick = {
                        notificationRationaleVisible = false
                        notificationPermission.launch(android.Manifest.permission.POST_NOTIFICATIONS)
                    },
                    modifier = Modifier.padding(start = 8.dp),
                )
            },
        )
    }
}
