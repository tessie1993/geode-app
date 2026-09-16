package dev.geode.ui

import android.app.PictureInPictureParams
import android.app.RemoteAction
import android.app.SearchManager
import android.content.Context
import android.content.ContextWrapper
import android.content.Intent
import android.content.res.Configuration
import android.graphics.Rect
import android.graphics.drawable.Icon
import android.os.Build
import android.os.Bundle
import android.provider.MediaStore
import android.util.Rational
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import androidx.annotation.OptIn
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.core.splashscreen.SplashScreen.Companion.installSplashScreen
import androidx.media3.common.Player
import androidx.media3.common.util.UnstableApi
import androidx.media3.session.PlaybackPendingIntentBuilder
import dagger.hilt.android.AndroidEntryPoint
import dev.geode.R
import dev.geode.playback.PlaybackService

/**
 * Picture-in-picture state shared across the Activity/Compose boundary.
 *
 * [VisualizerScreen] cannot see the Activity (it only sees a [Context]), and [MainActivity]
 * cannot see Compose state (`appState.expanded` lives inside `AppRoot`'s composition), so both
 * sides read and write this object instead of one holding a reference to the other. Every field
 * is written by exactly one side: [visualizerShowing] by `AppRoot` (true only while
 * [VisualizerScreen] is composed), [inPictureInPicture] by the Activity's own lifecycle callback,
 * and [canvasBoundsPx] by the visualizer canvas itself, which is the only place that knows where
 * it is on screen.
 */
internal object VisualizerPipCoordinator {
    var visualizerShowing by mutableStateOf(false)

    var inPictureInPicture by mutableStateOf(false)
        internal set

    /** Window-space bounds of the visualizer canvas, used for the PiP source rect hint and aspect. */
    var canvasBoundsPx: Rect? = null
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

    private fun importSharedPreset(intent: Intent?) {
        val data = intent?.data?.toString() ?: return
        val message =
            when (val result = visualsViewModel.importSharedPreset(data)) {
                PresetLinkImport.NotALink -> return
                is PresetLinkImport.Imported -> getString(R.string.preset_link_imported, result.name)
                PresetLinkImport.Unreadable -> getString(R.string.preset_link_unreadable)
            }
        Toast.makeText(this, message, Toast.LENGTH_LONG).show()
        intent.data = null
    }

    /**
     * Mirrors [importSharedPreset]: only consumes the intent's data once it is confirmed
     * to be a template link, so a preset link that reached here first is left alone for
     * it to have handled and a link neither of them recognises is left for the platform.
     */
    private fun importSharedTemplate(intent: Intent?) {
        val data = intent?.data?.toString() ?: return
        if (!visualsViewModel.isTemplateLink(data)) return
        intent.data = null
        visualsViewModel.importSharedTemplate(data) { result ->
            val message =
                when (result) {
                    is TemplateLinkImport.Imported -> getString(R.string.template_link_imported, result.name)
                    is TemplateLinkImport.Replaced -> getString(R.string.template_link_imported, result.name)
                    is TemplateLinkImport.Unreadable -> getString(R.string.template_link_unreadable)
                }
            Toast.makeText(this, message, Toast.LENGTH_LONG).show()
        }
    }

    /**
     * A finger swiping home while the visualizer is up and something is playing is the platform's
     * own signal that this is exactly the moment PiP exists for. On S+ this is redundant with
     * `setAutoEnterEnabled` (kept in sync by [refreshPipParams]), but on 26-30 the platform has no
     * auto-enter of its own, so this is the only path there is.
     */
    override fun onUserLeaveHint() {
        super.onUserLeaveHint()
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.S && autoEnterPipEligible()) {
            enterVisualizerPip()
        }
    }

    override fun onPictureInPictureModeChanged(
        isInPictureInPictureMode: Boolean,
        newConfig: Configuration,
    ) {
        super.onPictureInPictureModeChanged(isInPictureInPictureMode, newConfig)
        VisualizerPipCoordinator.inPictureInPicture = isInPictureInPictureMode
    }

    /** Called by the visualizer's own PiP control, and by [onUserLeaveHint] below S. */
    internal fun enterVisualizerPip() {
        runCatching { enterPictureInPictureMode(buildPipParams()) }
    }

    /**
     * Keeps the auto-enter flag and the play/pause remote action current while the visualizer is
     * on screen. Cheap to call often: [PictureInPictureParams] is immutable, so this just replaces
     * it — there is no running PiP session to disturb unless the platform is already showing one.
     */
    internal fun refreshPipParams() {
        runCatching { setPictureInPictureParams(buildPipParams()) }
    }

    private fun autoEnterPipEligible(): Boolean =
        settingsViewModel.guiPrefs.value.autoEnterPip &&
            VisualizerPipCoordinator.visualizerShowing &&
            playerViewModel.uiState.value.isPlaying

    private fun buildPipParams(): PictureInPictureParams {
        val builder = PictureInPictureParams.Builder().setActions(pipRemoteActions())
        VisualizerPipCoordinator.canvasBoundsPx?.let { bounds ->
            if (bounds.width() > 0 && bounds.height() > 0) {
                builder.setSourceRectHint(bounds)
                builder.setAspectRatio(clampedPipAspectRatio(bounds.width(), bounds.height()))
            }
        }
        // Below S, "auto enter" does not exist as a platform concept: onUserLeaveHint above is
        // the whole of it, and calling setAutoEnterEnabled there would not compile against minSdk.
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            builder.setAutoEnterEnabled(autoEnterPipEligible())
        }
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

    /** Clamps to the range the platform accepts (roughly 1:2.39 .. 2.39:1), preserving orientation. */
    private fun clampedPipAspectRatio(
        width: Int,
        height: Int,
    ): Rational {
        val ratio = width.toFloat() / height.toFloat()
        val clamped = ratio.coerceIn(PIP_MIN_ASPECT, PIP_MAX_ASPECT)
        return if (clamped >= ratio) {
            Rational((height * clamped).toInt().coerceAtLeast(1), height)
        } else {
            Rational(width, (width / clamped).toInt().coerceAtLeast(1))
        }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        importSharedPreset(intent)
        importSharedTemplate(intent)
        playFromSearch(intent)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        installSplashScreen().setKeepOnScreenCondition { !settingsViewModel.userDataLoaded.value }
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            AppRoot()
        }
        if (savedInstanceState == null) {
            importSharedPreset(intent)
            importSharedTemplate(intent)
            playFromSearch(intent)
        }
    }

    private companion object {
        /** The platform clamps `PictureInPictureParams.setAspectRatio` to this range. */
        const val PIP_MIN_ASPECT = 1f / 2.39f
        const val PIP_MAX_ASPECT = 2.39f
    }
}
