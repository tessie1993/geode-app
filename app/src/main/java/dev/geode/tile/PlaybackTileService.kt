package dev.geode.tile

import android.annotation.SuppressLint
import android.app.Activity
import android.app.PendingIntent
import android.content.ComponentName
import android.content.Intent
import android.graphics.drawable.Icon
import android.os.Build
import android.os.Bundle
import android.service.quicksettings.Tile
import android.service.quicksettings.TileService
import androidx.annotation.OptIn
import androidx.annotation.RequiresApi
import androidx.core.content.ContextCompat
import androidx.media3.common.Player
import androidx.media3.common.util.UnstableApi
import androidx.media3.common.util.Util
import androidx.media3.session.MediaController
import androidx.media3.session.SessionToken
import com.google.common.util.concurrent.ListenableFuture
import dev.geode.R
import dev.geode.appLaunchIntent
import dev.geode.playback.PlaybackService
import dev.geode.util.bestEffort

/**
 * The Quick Settings tile: play/pause on the one player, from the shade.
 *
 * It drives the player the way the widget does, through a [MediaController] on [PlaybackService]'s
 * session, never by reaching into `PlaybackEngine`. Going through the session is what makes a tap
 * here the same as a tap on the notification: the service takes audio focus, goes foreground and
 * posts its notification when playback starts, and it is the only path that works when the app's
 * UI has never been opened in this process. A controller from our own package is trusted, so the
 * session gives it every player command (see `PlaybackService.onConnect`).
 *
 * Not an active tile (no `ACTIVE_TILE` meta-data). An active tile is only redrawn when the app
 * calls [TileService.requestListeningState], which would mean a hook in the playback service on
 * every play, pause and track change. A listening tile is asked to redraw each time the shade shows
 * it, which is the only time anyone can see it, so the controller is held from [onStartListening]
 * to [onStopListening] and redraws on every player event in between. Binding the controller can
 * create the service when the process was cold; it is let go as soon as the shade closes, and the
 * service winds down with it unless something is playing.
 *
 * A tap with nothing loaded (a cold process, a cleared queue) opens the app instead of trying to
 * resume: the session would attempt playback resumption, and when nothing was ever played that
 * tap would silently do nothing. The app is where a track is picked, so the tile sends the user
 * there. A long press opens the app too ([TileLongPressActivity]); without it Android would open
 * the app's system info page, which is no use from a media tile.
 */
@OptIn(UnstableApi::class)
class PlaybackTileService : TileService() {
    private var pending: ListenableFuture<MediaController>? = null
    private var controller: MediaController? = null
    private var listening = false

    /** A tap that arrived before the controller connected; carried out on connect. */
    private var clickQueued = false

    private val redraw =
        object : Player.Listener {
            override fun onEvents(
                player: Player,
                events: Player.Events,
            ) = render()
        }

    override fun onStartListening() {
        super.onStartListening()
        listening = true
        connect()
        render()
    }

    override fun onStopListening() {
        listening = false
        if (!clickQueued) disconnect()
        super.onStopListening()
    }

    override fun onClick() {
        super.onClick()
        val connected = controller
        if (connected == null) {
            clickQueued = true
            connect()
        } else {
            act(connected)
        }
    }

    override fun onDestroy() {
        clickQueued = false
        disconnect()
        super.onDestroy()
    }

    private fun connect() {
        if (controller != null || pending != null) return
        val token = SessionToken(this, ComponentName(this, PlaybackService::class.java))
        val future = MediaController.Builder(this, token).buildAsync()
        pending = future
        future.addListener(
            {
                // A disconnect while this was in flight dropped [pending]; this future is stale.
                if (pending !== future) {
                    future.cancel(false)
                    return@addListener
                }
                pending = null
                val built = runCatching { future.get() }.getOrNull() ?: return@addListener
                controller = built
                built.addListener(redraw)
                if (clickQueued) {
                    clickQueued = false
                    act(built)
                }
                if (listening) render() else disconnect()
            },
            ContextCompat.getMainExecutor(this),
        )
    }

    private fun disconnect() {
        pending?.let { MediaController.releaseFuture(it) }
        pending = null
        controller?.let {
            it.removeListener(redraw)
            it.release()
        }
        controller = null
    }

    private fun act(player: Player) {
        if (player.currentMediaItem == null) {
            openApp()
            return
        }
        // Media3's own rule for the play/pause button: prepares an idle player, rewinds an ended
        // one, and pauses whatever is meant to be playing, buffering included.
        Util.handlePlayPauseButtonAction(player)
        render()
    }

    private fun openApp() {
        val intent = appLaunchIntent().addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        val launch =
            Runnable {
                bestEffort(TAG, "open the app from the tile") {
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
                        startActivityAndCollapse(
                            PendingIntent.getActivity(this, 0, intent, PendingIntent.FLAG_IMMUTABLE),
                        )
                    } else {
                        launchBelow34(intent)
                    }
                }
            }
        // The app does not show over the lock screen, so ask for the unlock first rather than
        // collapse the shade onto a keyguard that hides what was opened.
        if (isLocked) unlockAndRun(launch) else launch.run()
    }

    /** API 34 made the Intent overload throw; below it, it is the only one there is. Called only there. */
    @SuppressLint("StartActivityAndCollapseDeprecated")
    @Suppress("DEPRECATION")
    private fun launchBelow34(intent: Intent) = startActivityAndCollapse(intent)

    private fun render() {
        val tile = qsTile ?: return
        val player = controller
        val item = player?.currentMediaItem
        val playing = player != null && item != null && !Util.shouldShowPlayButton(player)
        val title =
            item
                ?.mediaMetadata
                ?.title
                ?.toString()
                ?.takeIf { it.isNotBlank() }
        tile.state = if (playing) Tile.STATE_ACTIVE else Tile.STATE_INACTIVE
        tile.icon = Icon.createWithResource(this, if (playing) R.drawable.ic_tile_pause else R.drawable.ic_tile_play)
        tile.label = getString(R.string.tile_label)
        tile.contentDescription =
            when {
                item == null -> getString(R.string.tile_open_app_description)
                playing -> getString(R.string.tile_pause_description, title ?: getString(R.string.tile_label))
                else -> getString(R.string.tile_play_description, title ?: getString(R.string.tile_label))
            }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            tile.subtitle =
                when {
                    item == null -> getString(R.string.tile_subtitle_idle)
                    else -> title ?: getString(if (playing) R.string.tile_state_playing else R.string.tile_state_paused)
                }
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) describeState(tile, playing, item != null)
        tile.updateTile()
    }

    @RequiresApi(Build.VERSION_CODES.R)
    private fun describeState(
        tile: Tile,
        playing: Boolean,
        loaded: Boolean,
    ) {
        tile.stateDescription =
            getString(
                when {
                    !loaded -> R.string.tile_state_idle
                    playing -> R.string.tile_state_playing
                    else -> R.string.tile_state_paused
                },
            )
    }

    private companion object {
        const val TAG = "PlaybackTile"
    }
}

/**
 * What a long press on the tile opens: the app, brought forward the way its launcher icon does.
 *
 * System UI starts whichever activity of ours answers `QS_TILE_PREFERENCES`. Pointing that filter
 * at `MainActivity` itself would stack a second instance of it on the task (it is a standard
 * launch mode activity, and every ViewModel and the player session hang off the one instance), so
 * this draws nothing and hands over to [appLaunchIntent], which reuses the running task.
 */
class TileLongPressActivity : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        startActivity(appLaunchIntent().addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
        finish()
    }
}
