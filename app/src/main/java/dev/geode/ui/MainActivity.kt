package dev.geode.ui

import android.app.PictureInPictureParams
import android.app.RemoteAction
import android.app.SearchManager
import android.content.ContentResolver
import android.content.Context
import android.content.ContextWrapper
import android.content.Intent
import android.content.pm.ActivityInfo
import android.content.res.Configuration
import android.graphics.Rect
import android.graphics.drawable.Icon
import android.net.Uri
import android.os.Bundle
import android.provider.MediaStore
import android.util.Rational
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.SystemBarStyle
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import androidx.annotation.OptIn
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.core.splashscreen.SplashScreen.Companion.installSplashScreen
import androidx.lifecycle.lifecycleScope
import androidx.media3.common.Player
import androidx.media3.common.util.UnstableApi
import androidx.media3.session.PlaybackPendingIntentBuilder
import dagger.hilt.android.AndroidEntryPoint
import dev.geode.R
import dev.geode.cast.CastController
import dev.geode.model.PresetLink
import dev.geode.model.ScreenRotation
import dev.geode.playback.PlaybackService
import dev.geode.ui.theme.isTelevision
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch

/**
 * Picture-in-picture state shared across the Activity/Compose boundary.
 *
 * Compose cannot see the Activity (it only sees a [Context]), and [MainActivity] cannot see
 * Compose state (the current destination lives inside `AppRoot`'s composition), so both sides
 * read and write this object instead of one holding a reference to the other. Every field is
 * written by exactly one side: [visualizerShowing] by `AppRoot` (true only while Visuals shows
 * the visual on this screen), [inPictureInPicture] by the Activity's own lifecycle callback, and
 * [canvasBoundsPx] by `AppRoot` around Visuals, whose visual fills it. While [inPictureInPicture],
 * Visuals hides its sheet and the shell its chrome, so the window shows the visual alone.
 */
internal object VisualizerPipCoordinator {
    var visualizerShowing by mutableStateOf(false)

    var inPictureInPicture by mutableStateOf(false)
        internal set

    /**
     * Window-space bounds of the visualizer canvas, used for the PiP source rect hint and aspect.
     * State, so `AppRoot` re-pushes the PiP params when a rotation or a window resize moves it.
     */
    var canvasBoundsPx by mutableStateOf<Rect?>(null)
}

/**
 * The aspect ratio (numerator, denominator) a PiP window of a [width] x [height] canvas asks for,
 * clamped to the range the platform accepts (roughly 1:2.39 .. 2.39:1), preserving orientation.
 */
internal fun pipAspectFraction(
    width: Int,
    height: Int,
): Pair<Int, Int> {
    val ratio = width.toFloat() / height.toFloat()
    val clamped = ratio.coerceIn(PIP_MIN_ASPECT, PIP_MAX_ASPECT)
    return if (clamped >= ratio) {
        Pair((height * clamped).toInt().coerceAtLeast(1), height)
    } else {
        Pair(width, (width / clamped).toInt().coerceAtLeast(1))
    }
}

/** The platform clamps `PictureInPictureParams.setAspectRatio` to this range. */
internal const val PIP_MIN_ASPECT = 1f / 2.39f
internal const val PIP_MAX_ASPECT = 2.39f

/**
 * Hands a shared link from the Activity's intent to the UI, which asks before importing
 * ([SharedLinkConfirm], drawn by `AppRoot` inside the theme). Activity-scoped state in a
 * process-wide object, like [VisualizerPipCoordinator]: there is one Activity.
 */
internal object SharedLinkInbox {
    var offer by mutableStateOf<SharedLinkOffer?>(null)
}

/**
 * A .milk a file manager opened in the app, imported and loaded: Visuals opens on its MilkDrop page,
 * once, from `AppRoot` (which holds the app state). Process-wide like [SharedLinkInbox].
 */
internal object MilkFileInbox {
    var opened by mutableStateOf(false)
}

/** Shows Visuals' MilkDrop page after [MilkFileInbox] took a .milk in. */
@Composable
internal fun MilkFileOpened(appState: GeodeAppState) {
    if (!MilkFileInbox.opened) return
    LaunchedEffect(Unit) {
        MilkFileInbox.opened = false
        appState.openVisualsPage(VisualsPage.MILKDROP)
    }
}

/** "Import shared preset?" / "Replace template?" for the link waiting in [SharedLinkInbox]. */
@Composable
internal fun SharedLinkConfirm() {
    val offer = SharedLinkInbox.offer ?: return
    val visualsViewModel: VisualsViewModel = geodeViewModel()
    val context = LocalContext.current
    val dismiss = { SharedLinkInbox.offer = null }
    val replaces = (offer as? SharedLinkOffer.Template)?.replaces
    val title =
        when {
            offer is SharedLinkOffer.Preset -> R.string.link_offer_preset_title
            offer is SharedLinkOffer.MilkFile -> R.string.link_offer_milk_title
            replaces != null -> R.string.link_offer_replace_title
            else -> R.string.link_offer_template_title
        }
    val body =
        when {
            offer is SharedLinkOffer.Preset ->
                stringResource(
                    R.string.link_offer_preset_body,
                    offer.name.ifBlank { stringResource(R.string.preset_shared_untitled) },
                )
            offer is SharedLinkOffer.MilkFile -> stringResource(R.string.link_offer_milk_body, offer.name)
            replaces != null -> stringResource(R.string.link_offer_replace_body, replaces)
            else ->
                stringResource(
                    R.string.link_offer_template_body,
                    offer.name.ifBlank { stringResource(R.string.link_offer_untitled_template) },
                )
        }
    val confirm =
        when {
            replaces != null -> R.string.link_offer_replace
            offer is SharedLinkOffer.MilkFile -> R.string.link_offer_open
            else -> R.string.link_offer_import
        }
    AlertDialog(
        onDismissRequest = dismiss,
        title = { Text(stringResource(title)) },
        text = { Text(body) },
        confirmButton = {
            TextButton(onClick = {
                dismiss()
                when (offer) {
                    is SharedLinkOffer.Preset -> importSharedPreset(context, visualsViewModel, offer.link)
                    is SharedLinkOffer.Template -> importSharedTemplate(context, visualsViewModel, offer.link)
                    is SharedLinkOffer.MilkFile -> openMilkFile(context, visualsViewModel, offer.link)
                }
            }) { Text(stringResource(confirm)) }
        },
        dismissButton = { TextButton(onClick = dismiss) { Text(stringResource(R.string.action_cancel)) } },
    )
}

private fun importSharedPreset(
    context: Context,
    visualsViewModel: VisualsViewModel,
    data: String,
) {
    val message =
        when (val result = visualsViewModel.importSharedPreset(data)) {
            PresetLinkImport.NotALink -> return
            is PresetLinkImport.Imported -> context.getString(R.string.preset_link_imported, result.name)
            PresetLinkImport.Unreadable -> context.getString(R.string.preset_link_unreadable)
        }
    Toast.makeText(context, message, Toast.LENGTH_LONG).show()
}

private fun openMilkFile(
    context: Context,
    visualsViewModel: VisualsViewModel,
    uri: String,
) {
    visualsViewModel.openMilkFile(Uri.parse(uri)) { ok ->
        if (ok) {
            MilkFileInbox.opened = true
        } else {
            Toast.makeText(context, R.string.milk_open_failed, Toast.LENGTH_LONG).show()
        }
    }
}

private fun importSharedTemplate(
    context: Context,
    visualsViewModel: VisualsViewModel,
    data: String,
) {
    if (!visualsViewModel.isTemplateLink(data)) return
    visualsViewModel.importSharedTemplate(data) { result ->
        val message =
            when (result) {
                is TemplateLinkImport.Imported -> context.getString(R.string.template_link_imported, result.name)
                is TemplateLinkImport.Replaced -> context.getString(R.string.template_link_imported, result.name)
                is TemplateLinkImport.Unreadable -> context.getString(R.string.template_link_unreadable)
            }
        Toast.makeText(context, message, Toast.LENGTH_LONG).show()
    }
}

/**
 * Unwraps to the hosting [MainActivity], if any — `LocalContext.current` is usually it directly,
 * but is not guaranteed to be.
 */
internal tailrec fun Context.findMainActivity(): MainActivity? =
    when (this) {
        is MainActivity -> this
        is ContextWrapper -> baseContext.findMainActivity()
        else -> null
    }

@OptIn(UnstableApi::class)
@AndroidEntryPoint
class MainActivity : ComponentActivity() {
    private val settingsViewModel: SettingsViewModel by viewModels()

    private val visualsViewModel: VisualsViewModel by viewModels()

    private val playerViewModel: PlayerViewModel by viewModels()

    private fun playFromSearch(intent: Intent?) {
        if (intent?.action != MediaStore.INTENT_ACTION_MEDIA_PLAY_FROM_SEARCH) return
        playerViewModel.playFromSearch(intent.getStringExtra(SearchManager.QUERY).orEmpty())
        intent.action = null
    }

    /**
     * A `geode://preset` or `geode://template` link. Anything can send one, so a readable link
     * is parked in [SharedLinkInbox] for the user to confirm; an unreadable one goes straight
     * to the import path, which writes nothing and says why. The intent's data is consumed only
     * when it was one of ours, so anything else is left for the platform.
     */
    private fun receiveSharedLink(intent: Intent?) {
        val data = intent?.data?.toString() ?: return
        val offer = visualsViewModel.offerFor(data)
        if (offer != null) {
            SharedLinkInbox.offer = offer
            intent.data = null
            return
        }
        if (PresetLink.isPresetLink(data) || visualsViewModel.isTemplateLink(data)) {
            importSharedPreset(this, visualsViewModel, data)
            importSharedTemplate(this, visualsViewModel, data)
            intent.data = null
        }
    }

    /**
     * A .milk opened from another app (ACTION_VIEW on a content or file uri). Anything can send one,
     * so like a shared link it waits in [SharedLinkInbox] for the user's yes before anything is
     * imported or loaded ([openMilkFile]). The uri is read only through the ContentResolver, only when
     * its name ends in .milk, capped in size and under a sanitised name; a refused one gets a toast.
     */
    private fun receiveMilkFile(intent: Intent?) {
        if (intent?.action != Intent.ACTION_VIEW) return
        val uri = intent.data ?: return
        if (uri.scheme != ContentResolver.SCHEME_CONTENT && uri.scheme != ContentResolver.SCHEME_FILE) return
        intent.data = null
        visualsViewModel.offerMilkFile(uri) { offer ->
            if (offer != null) {
                SharedLinkInbox.offer = offer
            } else {
                Toast.makeText(this, R.string.milk_open_failed, Toast.LENGTH_LONG).show()
            }
        }
    }

    override fun onPictureInPictureModeChanged(
        isInPictureInPictureMode: Boolean,
        newConfig: Configuration,
    ) {
        super.onPictureInPictureModeChanged(isInPictureInPictureMode, newConfig)
        VisualizerPipCoordinator.inPictureInPicture = isInPictureInPictureMode
    }

    /**
     * Keeps the auto-enter flag and the play/pause remote action current while Visuals is on
     * screen. Cheap to call often: [PictureInPictureParams] is immutable, so this just replaces
     * it — there is no running PiP session to disturb unless the platform is already showing one.
     */
    internal fun refreshPipParams() {
        runCatching { setPictureInPictureParams(buildPipParams()) }
    }

    // Never on a TV: TV PiP must start from the user's own action (TV app quality, TV-IE).
    private fun autoEnterPipEligible(): Boolean =
        !isTelevision() &&
            settingsViewModel.general.value.autoEnterPip &&
            VisualizerPipCoordinator.visualizerShowing &&
            playerViewModel.uiState.value.isPlaying

    private fun buildPipParams(): PictureInPictureParams {
        val builder = PictureInPictureParams.Builder().setActions(pipRemoteActions())
        VisualizerPipCoordinator.canvasBoundsPx?.let { bounds ->
            if (bounds.width() > 0 && bounds.height() > 0) {
                builder.setSourceRectHint(bounds)
                val (num, den) = pipAspectFraction(bounds.width(), bounds.height())
                builder.setAspectRatio(Rational(num, den))
            }
        }
        builder.setAutoEnterEnabled(autoEnterPipEligible())
        return builder.build()
    }

    /**
     * Play/pause and next, delivered to [PlaybackService] as media-button intents through
     * media3's own [PlaybackPendingIntentBuilder], exactly as its media notification does — the
     * PiP window is, in effect, one more remote. Play/pause may start the service in the
     * foreground when nothing is playing, since a paused session can have been stopped.
     */
    private fun pipRemoteActions(): ArrayList<RemoteAction> {
        val playing = playerViewModel.uiState.value.isPlaying
        val playPauseIcon = if (playing) R.drawable.ic_widget_pause else R.drawable.ic_widget_play
        val playPauseLabel = getString(if (playing) R.string.action_pause else R.string.action_play)
        val nextLabel = getString(R.string.action_next)
        val actions = ArrayList<RemoteAction>(2)
        val playPause =
            PlaybackPendingIntentBuilder(this, Player.COMMAND_PLAY_PAUSE, PlaybackService::class.java)
                .setStartAsForegroundService(!playing)
                .build()
        val next = PlaybackPendingIntentBuilder(this, Player.COMMAND_SEEK_TO_NEXT, PlaybackService::class.java).build()
        actions += RemoteAction(Icon.createWithResource(this, playPauseIcon), playPauseLabel, playPauseLabel, playPause)
        actions += RemoteAction(Icon.createWithResource(this, R.drawable.ic_widget_next), nextLabel, nextLabel, next)
        return actions
    }

    override fun onStart() {
        super.onStart()
        // A flag left set by a PiP window that ended without its callback would keep the dock away.
        VisualizerPipCoordinator.inPictureInPicture = isInPictureInPictureMode
        playerViewModel.onAppForeground()
    }

    /** Not called while the visualizer is in a PiP window, which is still on screen. */
    override fun onStop() {
        super.onStop()
        if (!isChangingConfigurations) playerViewModel.onAppBackground()
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        receiveSharedLink(intent)
        receiveAudioFile(this, intent, playerViewModel)
        receiveMilkFile(intent)
        playFromSearch(intent)
    }

    /**
     * Settings › Look › Screen rotation, live. No recreation follows a forced turn (`configChanges`
     * in the manifest), so Visuals' route and sheet stay as they were. Android ignores the request in
     * multi-window and, from targetSdk 36, on screens 600 dp and wider; a Presentation on a
     * connected display is another display and never turns with it.
     */
    private fun followScreenRotation() {
        lifecycleScope.launch {
            settingsViewModel.general.map { it.screenRotation }.distinctUntilChanged().collect {
                requestedOrientation =
                    when (it) {
                        ScreenRotation.FOLLOW -> ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED
                        ScreenRotation.PORTRAIT -> ActivityInfo.SCREEN_ORIENTATION_USER_PORTRAIT
                        ScreenRotation.LANDSCAPE -> ActivityInfo.SCREEN_ORIENTATION_USER_LANDSCAPE
                    }
            }
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        installSplashScreen().setKeepOnScreenCondition { !settingsViewModel.userDataLoaded.value }
        super.onCreate(savedInstanceState)
        // Cast to TV starts with the app (owner Q38), so a TV picked in the system's output
        // switcher is gated and streamed even before Visuals has shown the Cast button. Posted to
        // the decor view: the first frame's traversal runs ahead of it, so Cast's start-up (Play
        // services, MediaRouter) no longer holds the first frame on a cold start. A session the
        // switcher hands over before then still builds the controller (CastOptionsProvider).
        window.decorView.post { CastController.get(this) }
        // The UI is dark in every system mode, so the bar icons are always light: SystemBarStyle.auto
        // would turn them dark under a light system theme and leave them invisible on black.
        enableEdgeToEdge(
            statusBarStyle = SystemBarStyle.dark(android.graphics.Color.TRANSPARENT),
            navigationBarStyle = SystemBarStyle.dark(android.graphics.Color.TRANSPARENT),
        )
        followScreenRotation()
        setContent {
            AppRoot()
        }
        if (savedInstanceState == null) {
            receiveSharedLink(intent)
            receiveAudioFile(this, intent, playerViewModel)
            receiveMilkFile(intent)
            playFromSearch(intent)
        }
    }
}
