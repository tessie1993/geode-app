package dev.geode

import android.app.PictureInPictureParams
import android.app.RemoteAction
import android.app.SearchManager
import android.content.Intent
import android.content.res.Configuration
import android.graphics.drawable.Icon
import android.os.Build
import android.os.Bundle
import android.util.Rational
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import androidx.annotation.OptIn
import androidx.media3.common.Player
import androidx.media3.common.util.UnstableApi
import androidx.media3.session.PlaybackPendingIntentBuilder
import dagger.hilt.android.AndroidEntryPoint
import dev.geode.data.MotionPrefs
import dev.geode.nav.DeepLink
import dev.geode.nav.NavSaver
import dev.geode.nav.Navigator
import dev.geode.nav.connect.MotionPolicy
import dev.geode.nav.connect.NavConnectors
import dev.geode.nav.platform.SensorGravitySource
import dev.geode.nav.platform.bindBack
import dev.geode.playback.PlaybackService
import dev.geode.ui.OpalineApp
import dev.geode.ui.PlayerViewModel
import dev.geode.ui.SettingsViewModel
import dev.geode.ui.VisualizerPipCoordinator
import kotlinx.coroutines.flow.MutableStateFlow

/** Native navigation, media and lifecycle host for the Opaline interface. */
@OptIn(UnstableApi::class)
@AndroidEntryPoint
class MainActivity : ComponentActivity() {
    lateinit var navigator: Navigator
        private set

    lateinit var connectors: NavConnectors
        private set

    val motion = MutableStateFlow(MotionPolicy())
    private val playerViewModel: PlayerViewModel by viewModels()
    private val settingsViewModel: SettingsViewModel by viewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        motion.value = MotionPolicy(reducedMotion = MotionPrefs.reducedMotion(geodeContainer.prefsFiles.general))
        val pending =
            DeepLink.parse(
                savedInstanceState?.getString(KEY_LINK_ACTION),
                savedInstanceState?.getString(KEY_LINK_DATA),
                savedInstanceState?.getString(KEY_LINK_QUERY),
            )
        navigator = Navigator(NavSaver.decode(savedInstanceState?.getString(KEY_NAV)), pending)
        connectors = NavConnectors(motion = motion, gravity = SensorGravitySource(this))
        navigator.bindBack(onBackPressedDispatcher, this)
        route(intent)
        setContent { OpalineApp(navigator, connectors, motion) }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        route(intent)
    }

    override fun onStart() {
        super.onStart()
        connectors.gravity.start()
    }

    override fun onStop() {
        connectors.gravity.stop()
        super.onStop()
    }

    override fun onSaveInstanceState(outState: Bundle) {
        super.onSaveInstanceState(outState)
        outState.putString(KEY_NAV, NavSaver.encode(navigator.state.value))
        when (val pending = navigator.pendingLink.value) {
            is DeepLink.Preset -> outState.putString(KEY_LINK_DATA, pending.link)
            is DeepLink.Template -> outState.putString(KEY_LINK_DATA, pending.link)
            is DeepLink.PlayFromSearch -> {
                outState.putString(KEY_LINK_ACTION, DeepLink.ACTION_PLAY_FROM_SEARCH)
                outState.putString(KEY_LINK_QUERY, pending.query)
            }
            null -> Unit
        }
    }

    private fun route(intent: Intent?) {
        val link = DeepLink.parse(intent?.action, intent?.dataString, intent?.getStringExtra(SearchManager.QUERY)) ?: return
        navigator.handle(link)
        intent?.action = null
        intent?.data = null
    }

    override fun onPictureInPictureModeChanged(
        isInPictureInPictureMode: Boolean,
        newConfig: Configuration,
    ) {
        super.onPictureInPictureModeChanged(isInPictureInPictureMode, newConfig)
        VisualizerPipCoordinator.inPictureInPicture = isInPictureInPictureMode
    }

    override fun onUserLeaveHint() {
        super.onUserLeaveHint()
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.S && autoPip()) enterVisualizerPip()
    }

    fun enterVisualizerPip() {
        if (!packageManager.hasSystemFeature(android.content.pm.PackageManager.FEATURE_PICTURE_IN_PICTURE)) return
        runCatching { enterPictureInPictureMode(pipParams()) }
    }

    fun refreshPipParams() {
        if (packageManager.hasSystemFeature(android.content.pm.PackageManager.FEATURE_PICTURE_IN_PICTURE)) {
            runCatching { setPictureInPictureParams(pipParams()) }
        }
    }

    private fun autoPip(): Boolean =
        settingsViewModel.guiPrefs.value.autoEnterPip &&
            VisualizerPipCoordinator.visualizerShowing &&
            playerViewModel.uiState.value.isPlaying

    private fun pipParams(): PictureInPictureParams {
        val playing = playerViewModel.uiState.value.isPlaying
        val label = getString(if (playing) R.string.action_pause else R.string.action_play)
        val play =
            PlaybackPendingIntentBuilder(this, Player.COMMAND_PLAY_PAUSE, PlaybackService::class.java)
                .setStartAsForegroundService(!playing)
                .build()
        val next = PlaybackPendingIntentBuilder(this, Player.COMMAND_SEEK_TO_NEXT, PlaybackService::class.java).build()
        val params =
            PictureInPictureParams.Builder().setActions(
                listOf(
                    RemoteAction(
                        Icon.createWithResource(this, if (playing) R.drawable.ic_widget_pause else R.drawable.ic_widget_play),
                        label,
                        label,
                        play,
                    ),
                    RemoteAction(
                        Icon.createWithResource(this, R.drawable.ic_widget_next),
                        getString(R.string.action_next),
                        getString(R.string.action_next),
                        next,
                    ),
                ),
            )
        VisualizerPipCoordinator.canvasBoundsPx?.let { bounds ->
            if (bounds.height() > 0 && bounds.width() > 0) {
                val ratio = (bounds.width().toFloat() / bounds.height()).coerceIn(1f / 2.39f, 2.39f)
                params.setSourceRectHint(bounds).setAspectRatio(Rational((ratio * 1000).toInt(), 1000))
            }
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) params.setAutoEnterEnabled(autoPip())
        return params.build()
    }

    private companion object {
        const val KEY_NAV = "nav"
        const val KEY_LINK_ACTION = "pending_link_action"
        const val KEY_LINK_DATA = "pending_link_data"
        const val KEY_LINK_QUERY = "pending_link_query"
    }
}
