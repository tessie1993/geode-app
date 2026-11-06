package dev.geode.cast

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.SystemClock
import android.widget.Toast
import androidx.media3.common.Player
import dev.geode.R
import dev.geode.RingLog
import dev.geode.appLaunchIntent
import dev.geode.billing.ProSnapshot
import dev.geode.playback.PlaybackEngine
import dev.geode.playback.PlaybackSession
import dev.geode.pro.ProFeature
import dev.geode.startedInForeground

/**
 * Keeps a cast running with the app in the background or the screen off (owner, 2026-11-02):
 * a foreground service of type `mediaPlayback` ("continue video playback from the background"),
 * declared in Play Console with the other three (docs/play/CONSOLE_CHECKLIST.md § 7).
 *
 * It owns the [CastStream] and silences the phone while it runs: the sound plays on the TV,
 * inside the stream, and the phone is the remote. The music's player keeps playing (its PCM tap
 * comes before the volume, so the visuals and the stream still hear it); only its volume goes to
 * zero, and anything that raises it while casting (a crossfade, the sleep timer's fade) is put back.
 *
 * Pro is checked again every minute ([recheckPro]). A trial that runs out mid-cast lets the song
 * finish, then ends the cast with a Go Pro note (owner). A stream that fails (won't start, freezes,
 * the renderer dies) ends the cast at once, with the sound back on the phone and a short message;
 * no silent restart (owner).
 */
class CastService : Service() {
    private val handler = Handler(Looper.getMainLooper())
    private var stream: CastStream? = null
    private var playback: PlaybackSession? = null
    private var savedVolume = 1f
    private var waitingSinceMs = 0L

    /** Set when the trial ran out mid-cast: the cast ends when this song does. */
    private var stopAtSongEnd = false
    private var failed = false

    private val keepMuted =
        object : Player.Listener {
            override fun onVolumeChanged(volume: Float) {
                if (volume > 0f) playback?.player?.volume = 0f
            }

            override fun onMediaItemTransition(
                mediaItem: androidx.media3.common.MediaItem?,
                reason: Int,
            ) = endIfTrialOver()

            override fun onPlaybackStateChanged(playbackState: Int) {
                if (playbackState == Player.STATE_ENDED) endIfTrialOver()
            }
        }

    private fun endIfTrialOver() {
        if (!stopAtSongEnd) return
        stopAtSongEnd = false
        Toast.makeText(this, R.string.cast_trial_ended, Toast.LENGTH_LONG).show()
        CastController.get(this).onTrialEnded()
        stopSelf()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(
        intent: Intent?,
        flags: Int,
        startId: Int,
    ): Int {
        if (intent?.action == ACTION_STOP) {
            CastController.get(this).stop()
            stopSelf()
            return START_NOT_STICKY
        }
        // fail() ends the cast session too, so the TV is not left waiting on a stream.
        if (!startedInForeground(TAG, ::startInForeground, ::fail)) return START_NOT_STICKY
        if (stream == null) startStream()
        return START_NOT_STICKY
    }

    private fun startStream() {
        mutePhone()
        val started = runCatching { CastStream(this, playback?.ingest ?: 0L) { handler.post { fail() } }.also { it.start() } }
        started.onFailure { RingLog.note(TAG, "stream did not start: $it") }
        val s = started.getOrNull() ?: return fail()
        stream = s
        waitingSinceMs = SystemClock.elapsedRealtime()
        handler.post(waitForStream)
        handler.postDelayed(recheckPro, PRO_RECHECK_MS)
    }

    /** Ends the cast because the stream can't run; onDestroy gives the phone its sound back. */
    private fun fail() {
        if (failed) return
        failed = true
        Toast.makeText(this, R.string.cast_failed, Toast.LENGTH_LONG).show()
        CastController.get(this).onStreamFailed()
        stopSelf()
    }

    private val recheckPro: Runnable =
        object : Runnable {
            override fun run() {
                if (!ProSnapshot.gate(this@CastService).allows(ProFeature.CAST)) {
                    // Let the song finish (owner); the player listener ends the cast then.
                    stopAtSongEnd = true
                    return
                }
                handler.postDelayed(this, PRO_RECHECK_MS)
            }
        }

    private val watchFreeze: Runnable =
        object : Runnable {
            override fun run() {
                val s = stream ?: return
                if (s.isFrozen) {
                    RingLog.note(TAG, "no segment for ${CastStream.FROZEN_MS / 1000} s: ending the cast")
                    fail()
                } else {
                    handler.postDelayed(this, POLL_MS * 4)
                }
            }
        }

    private val waitForStream: Runnable =
        object : Runnable {
            override fun run() {
                val s = stream ?: return
                when {
                    s.isReady -> {
                        CastController.get(this@CastService).onStreamReady(s.playlistUrl)
                        handler.post(watchFreeze)
                    }
                    SystemClock.elapsedRealtime() - waitingSinceMs > START_TIMEOUT_MS -> {
                        RingLog.note(TAG, "stream not ready after ${START_TIMEOUT_MS / 1000} s")
                        fail()
                    }
                    else -> handler.postDelayed(this, POLL_MS)
                }
            }
        }

    private fun mutePhone() {
        val session = PlaybackEngine.acquireForService(this)
        playback = session
        savedVolume = session.player.volume
        session.player.addListener(keepMuted)
        session.player.volume = 0f
    }

    private fun unmutePhone() {
        val session = playback ?: return
        playback = null
        session.player.removeListener(keepMuted)
        session.player.volume = savedVolume
        PlaybackEngine.releaseService()
    }

    override fun onDestroy() {
        handler.removeCallbacksAndMessages(null)
        unmutePhone()
        val s = stream
        stream = null
        // Stopping joins the render thread and closes the codecs: off the main thread.
        if (s != null) Thread({ s.stop() }, "cast-stop").start()
        super.onDestroy()
    }

    private fun startInForeground() {
        val manager = getSystemService(NotificationManager::class.java)
        manager.createNotificationChannel(
            NotificationChannel(CHANNEL_ID, getString(R.string.cast_channel_name), NotificationManager.IMPORTANCE_LOW).apply {
                description = getString(R.string.cast_channel_description)
                setShowBadge(false)
            },
        )
        val open = PendingIntent.getActivity(this, 0, appLaunchIntent(), PendingIntent.FLAG_IMMUTABLE)
        val stop =
            PendingIntent.getService(
                this,
                0,
                Intent(this, CastService::class.java).setAction(ACTION_STOP),
                PendingIntent.FLAG_IMMUTABLE,
            )
        val notification =
            Notification
                .Builder(this, CHANNEL_ID)
                .setContentTitle(getString(R.string.cast_notification_title))
                .setContentText(getString(R.string.cast_notification_text))
                .setSmallIcon(R.drawable.ic_stat_capture)
                .setOngoing(true)
                .setContentIntent(open)
                .addAction(Notification.Action.Builder(null, getString(R.string.cast_stop), stop).build())
                .build()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            startForeground(NOTIFICATION_ID, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK)
        } else {
            startForeground(NOTIFICATION_ID, notification)
        }
    }

    companion object {
        private const val TAG = "CastService"
        private const val CHANNEL_ID = "cast"
        private const val NOTIFICATION_ID = 0xCA57
        private const val ACTION_STOP = "dev.geode.cast.STOP"
        private const val POLL_MS = 250L
        private const val PRO_RECHECK_MS = 60_000L

        /** Enough for the three segments the receiver starts on, with room for a slow first encode. */
        private const val START_TIMEOUT_MS = 15_000L

        /** Starts the service; false if Android refused (a background start it doesn't allow). */
        fun start(context: Context): Boolean =
            runCatching { context.startForegroundService(Intent(context, CastService::class.java)) }
                .onFailure { RingLog.note(TAG, "start refused: $it") }
                .isSuccess

        fun stop(context: Context) {
            context.stopService(Intent(context, CastService::class.java))
        }
    }
}
