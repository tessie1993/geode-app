package dev.geode.ui

import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.listSaver
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.core.content.ContextCompat
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.export.ExportAspect
import dev.geode.export.ExportCodec
import dev.geode.export.TimeOfDayDrift
import dev.geode.render.SceneFactory
import dev.geode.render.VisualizerView

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

/**
 * Asks for POST_NOTIFICATIONS once, right as the first render starts, with one rationale dialog
 * first. On API 33+ a render's progress notification (ExportService, foreground since it must
 * survive the Activity going away) is silently suppressed for anyone who has never granted it, and
 * nothing else in the app asks unless audio capture is turned on. The render proceeds either way.
 * Returns the ask; the rationale dialog is drawn where this is called.
 */
@Composable
internal fun rememberRenderNotificationAsk(): () -> Unit {
    val context = LocalContext.current
    var asked by rememberSaveable { mutableStateOf(false) }
    var rationaleVisible by rememberSaveable { mutableStateOf(false) }
    val permission = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { }
    if (rationaleVisible) {
        AlertDialog(
            onDismissRequest = { rationaleVisible = false },
            title = { Text(stringResource(R.string.export_notification_permission_title)) },
            text = { Text(stringResource(R.string.export_notification_permission_body)) },
            confirmButton = {
                TextButton(
                    onClick = {
                        rationaleVisible = false
                        permission.launch(android.Manifest.permission.POST_NOTIFICATIONS)
                    },
                ) { Text(stringResource(R.string.action_ok)) }
            },
            dismissButton = {
                TextButton(onClick = { rationaleVisible = false }) {
                    Text(stringResource(R.string.export_notification_permission_skip))
                }
            },
        )
    }
    return {
        val alreadyGranted =
            Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU ||
                ContextCompat.checkSelfPermission(context, android.Manifest.permission.POST_NOTIFICATIONS) ==
                PackageManager.PERMISSION_GRANTED
        if (!asked && !alreadyGranted) {
            asked = true
            rationaleVisible = true
        }
    }
}

/**
 * Starts one visualizer export of the current track through [PlayerViewModel.startExport] (which
 * holds the Pro gates). Passing true first asks where to save; below Q it always asks:
 * saving into the Videos library without asking is a scoped-storage privilege, and below Q the same
 * insert needs WRITE_EXTERNAL_STORAGE, a permission this app does not ask for, so the write failed
 * with a SecurityException the user saw raw. The folder picker is the same save by another route.
 */
@Composable
internal fun rememberVisualizerExport(
    viewModel: PlayerViewModel,
    visualizerView: VisualizerView,
): (PendingExport, Boolean) -> Unit {
    val askNotifications = rememberRenderNotificationAsk()
    var pendingExport by rememberSaveable(stateSaver = PendingExportSaver) {
        mutableStateOf<PendingExport?>(null)
    }
    val sceneFactoryFor: (String, String) -> SceneFactory =
        { requested, fallback ->
            val renderer = visualizerView.visualizerRenderer
            renderer.exportSceneFactory(
                if (requested in renderer.availableSceneIds()) requested else fallback,
            )
        }
    val start: (PendingExport, Uri?) -> Unit = { req, dest ->
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
    val destinationPicker =
        rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("video/mp4")) { dest ->
            val req = pendingExport
            pendingExport = null
            if (dest != null && req != null) start(req, dest)
        }
    return { req, toFolder ->
        askNotifications()
        if (toFolder || Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) {
            pendingExport = req
            destinationPicker.launch("muzviz_${System.currentTimeMillis()}.mp4")
        } else {
            start(req, null)
        }
    }
}

/**
 * The loop render sheet, opened from Studio › Render. A loop render already under way, finished
 * or failed reopens onto its own state; the standard export lives on the Render page itself.
 */
@Composable
fun ExportHost(
    viewModel: PlayerViewModel,
    visualizerView: VisualizerView,
    onDismiss: () -> Unit,
) {
    val studioViewModel: StudioViewModel = geodeViewModel()
    val viz by viewModel.vizState.collectAsStateWithLifecycle()
    val loop by studioViewModel.loopState.collectAsStateWithLifecycle()
    val askNotifications = rememberRenderNotificationAsk()
    var pendingLoopExport by rememberSaveable(stateSaver = PendingLoopExportSaver) {
        mutableStateOf<PendingLoopExport?>(null)
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
    LoopRenderSheet(
        state = loop,
        onStart = { req ->
            askNotifications()
            if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) {
                pendingLoopExport = PendingLoopExport.from(req)
                loopDestinationPicker.launch("muzviz_loop_${System.currentTimeMillis()}.mp4")
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
            askNotifications()
            pendingLoopExport = PendingLoopExport.from(req)
            loopDestinationPicker.launch("muzviz_loop_${System.currentTimeMillis()}.mp4")
        },
        onCancel = studioViewModel::cancelLoopRender,
        onDismiss = {
            studioViewModel.clearLoopResult()
            onDismiss()
        },
    )
}
