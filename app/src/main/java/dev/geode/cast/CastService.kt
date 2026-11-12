package dev.geode.cast

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.content.pm.ServiceInfo
import android.net.wifi.WifiManager
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.SystemClock
import android.widget.Toast
import androidx.media3.common.Player
import dev.geode.LONG_WORK_WAKE_MS
import dev.geode.R
import dev.geode.RingLog
import dev.geode.ServiceWakeLock
import dev.geode.appLaunchIntent
import dev.geode.billing.ProSnapshot
import dev.geode.engine.bridge.GeodeNative
import dev.geode.playback.PlaybackEngine
import dev.geode.playback.PlaybackSession
import dev.geode.pro.ProFeature
import dev.geode.startedInForeground
import dev.geode.util.bestEffort
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

/**
 * Keeps a cast running with the app in the background or the screen off (owner, 2026-11-02):
 * a foreground service of type `mediaPlayback` ("continue video playback from the background"),
 * declared in Play Console (Policy > App content > Foreground service permissions) with the
 * other three types the manifest uses.
 *
 * It owns the [CastStream], which serves a TV ([Target.TV], [CastController]) and a browser through "Show on
 * a computer" ([Target.LINK], [ComputerLink]): one stream, one encoder, for either or both; it runs while one
 * of them wants it. It silences the phone while it runs: the sound plays on the TV or the computer, inside the
 * stream (a few seconds behind the phone, so both at once would echo), and the phone is the remote. The
 * music's player keeps playing (its PCM tap comes before the volume, so the visuals and the stream still hear it); only its volume goes to
 * zero, and anything that raises it while casting (a crossfade, the sleep timer's fade) is put back.
 *
 * Pro is checked again every minute ([recheckPro]). A trial that runs out mid-cast lets the song
 * finish, then ends the cast with a Go Pro note (owner). A stream that fails (won't start, freezes,
 * the renderer dies) ends the cast at once, with the sound back on the phone and a short message;
 * no silent restart (owner). So does losing the network it serves from: the link would name nothing.
 */
class CastService : Service() {
    private val handler = Handler(Looper.getMainLooper())
    private var stream: CastStream? = null
    private var playback: PlaybackSession? = null
    private var savedVolume = 1f
    private var waitingSinceMs = 0L

    /** Who wants the stream; the service stops when neither does. */
    private var forTv = false
    private var forLink = false
    private var ready = false

    /** When the TV was handed the playlist (SystemClock.elapsedRealtime), 0 = not yet; [checkReceiver] judges from it. */
    private var tvLoadAtMs = 0L
    private val receiverStatus = IntArray(2)

    /** Set when the trial ran out mid-cast: the cast ends when this song does. */
    private var stopAtSongEnd = false
    private var failed = false
    private val wakeLock by lazy { ServiceWakeLock(this, TAG) }

    /**
     * The TV fetches every segment from this phone: with the screen off Wi-Fi drops into power save and
     * answers late. Android 14 and later only honour the lock with the screen on; there it costs nothing.
     */
    @Suppress("DEPRECATION")
    private val wifiLock by lazy {
        getSystemService(WifiManager::class.java)
            ?.createWifiLock(WifiManager.WIFI_MODE_FULL_HIGH_PERF, "geode:$TAG")
            ?.apply { setReferenceCounted(false) }
    }

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
        if (forTv) {
            Toast.makeText(this, R.string.cast_trial_ended, Toast.LENGTH_LONG).show()
            CastController.get(this).onTrialEnded()
        }
        if (forLink) ComputerLink.onFailed(GeodeNative.LINK_REASON_TRIAL)
        stopSelf()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(
        intent: Intent?,
        flags: Int,
        startId: Int,
    ): Int {
        val action = intent?.action
        if (action == ACTION_STOP) {
            if (forTv) CastController.get(this).stop()
            if (forLink) ComputerLink.onServiceStopped()
            stopSelf()
            return START_NOT_STICKY
        }
        if (action == ACTION_RELEASE_TV || action == ACTION_RELEASE_LINK) {
            if (action == ACTION_RELEASE_TV) forTv = false else forLink = false
            if (!forTv) tvLoadAtMs = 0L
            applyPeers()
            // The other one keeps the stream; its notification says who has it now.
            if (forTv || forLink) bestEffort(TAG, "startInForeground()") { startInForeground() } else stopSelf()
            return START_NOT_STICKY
        }
        // A mic change reaching a cast that has already ended must not bring the service back.
        if (action == ACTION_MIC && stream == null) {
            stopSelf()
            return START_NOT_STICKY
        }
        when (action) {
            ACTION_LINK -> forLink = true
            ACTION_MIC -> Unit
            else -> forTv = true
        }
        // fail() ends the cast session too, so the TV is not left waiting on a stream.
        if (!startedInForeground(TAG, ::startInForeground) { fail(CastFailure.BACKGROUND_START) }) return START_NOT_STICKY
        running = true
        when {
            action == ACTION_MIC -> Unit
            stream == null -> startStream()
            ready -> announceReady()
        }
        applyPeers()
        return START_NOT_STICKY
    }

    /**
     * Who the server lets in, by address (native, `geode_link_admit`): the Cast device alone while a TV casts,
     * the Wi-Fi's subnet while a link is shared. Anyone on the Wi-Fi can read the playlist's URL from the TV's
     * receiver, so on a TV cast the token alone would not keep them out.
     */
    private fun applyPeers() {
        val s = stream ?: return
        s.admit(if (forTv) CastController.get(this).tvAddress() else null, forTv, forLink)
    }

    /** Hands the running stream to whoever wants it: the TV its playlist, the sheet its link. */
    private fun announceReady() {
        val s = stream ?: return
        if (forTv) {
            tvLoadAtMs = SystemClock.elapsedRealtime()
            CastController.get(this).onStreamReady(s.playlistUrl)
        }
        if (forLink) ComputerLink.onReady(s.shareUrl, s.shareLabel)
    }

    private fun startStream() {
        mutePhone()
        val started = runCatching { CastStream(this, playback?.sampleRing) { handler.post { fail() } }.also { it.start() } }
        started.onFailure { RingLog.note(TAG, "stream did not start: $it") }
        val noLan = started.exceptionOrNull() is CastStream.NoLanAddress
        val s = started.getOrNull() ?: return fail(if (noLan) CastFailure.NO_LAN else CastFailure.STREAM)
        stream = s
        applyPeers()
        // The phone encodes the picture; with the music paused nothing else keeps the CPU awake.
        wakeLock.acquire(LONG_WORK_WAKE_MS)
        bestEffort(TAG, "wifiLock.acquire()") { wifiLock?.acquire() }
        waitingSinceMs = SystemClock.elapsedRealtime()
        handler.post(waitForStream)
        handler.postDelayed(recheckPro, PRO_RECHECK_MS)
    }

    /** Ends the cast because the stream can't run; onDestroy gives the phone its sound back. */
    private fun fail(reason: CastFailure = CastFailure.STREAM) {
        if (failed) return
        failed = true
        if (forTv) CastController.get(this).onStreamFailed(reason)
        if (forLink) ComputerLink.onFailed(reason.linkReason)
        stopSelf()
    }

    private val recheckPro: Runnable =
        object : Runnable {
            override fun run() {
                if (!ProSnapshot.gate(this@CastService).allows(ProFeature.CAST)) {
                    // Let the song finish (owner); the player listener ends the cast then. With nothing playing
                    // there is no song to wait for, so it ends now.
                    stopAtSongEnd = true
                    if (playback?.player?.isPlaying != true) endIfTrialOver()
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
                } else if (s.address !in CastStream.lanAddresses(this@CastService)) {
                    RingLog.note(TAG, "the network the stream serves from went away: ending the cast")
                    fail(CastFailure.NETWORK_CHANGED)
                } else {
                    checkReceiver(s)
                    handler.postDelayed(this, POLL_MS * 4)
                }
            }
        }

    /**
     * Ends the TV's part when the TV is not playing the stream (native, `geode_link_receiver_health`): its
     * receiver reported an error, went idle, or never fetched (or stopped fetching while it says it plays), as
     * on a guest Wi-Fi that keeps devices apart. Without this the phone stays muted and the button says
     * Casting while the TV shows a spinner. A browser watching the link keeps the stream.
     */
    private fun checkReceiver(s: CastStream) {
        if (!forTv || tvLoadAtMs == 0L) return
        val controller = CastController.get(this)
        controller.receiverStatus(receiverStatus)
        val health =
            GeodeNative.linkReceiverHealth(
                SystemClock.elapsedRealtime(),
                tvLoadAtMs,
                s.lastReceiverFetchMs,
                receiverStatus[0],
                receiverStatus[1],
            )
        if (health == GeodeNative.LINK_RECEIVER_OK) return
        RingLog.note(TAG, "the TV is not playing the stream ($health, player ${receiverStatus[0]}, idle ${receiverStatus[1]})")
        tvLoadAtMs = 0L
        when (health) {
            GeodeNative.LINK_RECEIVER_ERROR -> controller.onStreamFailed(CastFailure.TV_COULD_NOT_PLAY)
            GeodeNative.LINK_RECEIVER_UNREACHABLE -> controller.onStreamFailed(CastFailure.TV_UNREACHABLE)
            else -> controller.stop()
        }
    }

    private val waitForStream: Runnable =
        object : Runnable {
            override fun run() {
                val s = stream ?: return
                when {
                    s.isReady -> {
                        ready = true
                        announceReady()
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
        running = false
        micHeldFlow.value = false
        handler.removeCallbacksAndMessages(null)
        wakeLock.release()
        bestEffort(TAG, "wifiLock.release()") { wifiLock?.takeIf { it.isHeld }?.release() }
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
        // A TV named first: with both watching, the TV is what the user picked from the system's output list too.
        val title = if (forTv) R.string.cast_notification_title else R.string.link_notification_title
        // With Live input on, the notification says the mic is listening (and why): it runs on with the app closed.
        val micOn = micWanted && checkSelfPermission(android.Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED
        val text =
            when {
                forTv && micOn -> R.string.cast_notification_text_mic
                forTv -> R.string.cast_notification_text
                micOn -> R.string.link_notification_text_mic
                else -> R.string.link_notification_text
            }
        val stopLabel = if (forTv) R.string.cast_stop else R.string.link_stop
        val notification =
            Notification
                .Builder(this, CHANNEL_ID)
                .setContentTitle(getString(title))
                .setContentText(getString(text))
                .setSmallIcon(R.drawable.ic_stat_capture)
                .setOngoing(true)
                .setContentIntent(open)
                .addAction(Notification.Action.Builder(null, getString(stopLabel), stop).build())
                .build()
        // With the mic on, the microphone type keeps it hearing in the background. Android grants it only while
        // the app is visible (or the service was started then); refused, the cast goes on without it.
        val playbackType = ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK
        val withMic =
            micOn &&
                runCatching {
                    startForeground(
                        NOTIFICATION_ID,
                        notification,
                        playbackType or ServiceInfo.FOREGROUND_SERVICE_TYPE_MICROPHONE,
                    )
                }.onFailure { RingLog.note(TAG, "no microphone type: $it") }
                    .isSuccess
        if (!withMic) startForeground(NOTIFICATION_ID, notification, playbackType)
        micHeldFlow.value = withMic
    }

    companion object {
        private const val TAG = "CastService"
        private const val CHANNEL_ID = "cast"
        private const val NOTIFICATION_ID = 0xCA57
        private const val ACTION_STOP = "dev.geode.cast.STOP"
        private const val ACTION_MIC = "dev.geode.cast.MIC"
        private const val ACTION_LINK = "dev.geode.cast.LINK"
        private const val ACTION_RELEASE_TV = "dev.geode.cast.RELEASE_TV"
        private const val ACTION_RELEASE_LINK = "dev.geode.cast.RELEASE_LINK"
        private const val POLL_MS = 250L
        private const val PRO_RECHECK_MS = 60_000L

        /** Enough for the three segments the receiver starts on, with room for a slow first encode. */
        private const val START_TIMEOUT_MS = 15_000L

        @Volatile
        private var running = false

        @Volatile
        private var micWanted = false
        private val micHeldFlow = MutableStateFlow(false)

        /** True while a cast holds the microphone foreground type, so the mic may keep running in the background. */
        val micHeld: StateFlow<Boolean> = micHeldFlow

        /**
         * The mic started or stopped. A running cast takes or drops the microphone type; call it while the app
         * is visible, the only time Android grants that type.
         */
        fun setMicWanted(
            context: Context,
            on: Boolean,
        ) {
            if (micWanted == on) return
            micWanted = on
            if (!running) return
            runCatching { context.startService(Intent(context, CastService::class.java).setAction(ACTION_MIC)) }
                .onFailure { RingLog.note(TAG, "mic update refused: $it") }
        }

        /**
         * Starts the stream for [target], or hands the running one to it; returns why Android refused (a
         * background start it doesn't allow), or null.
         */
        fun start(
            context: Context,
            target: Target,
        ): Throwable? {
            val intent = Intent(context, CastService::class.java)
            if (target == Target.LINK) intent.action = ACTION_LINK
            return runCatching { context.startForegroundService(intent) }
                .onFailure { RingLog.note(TAG, "start refused: $it") }
                .exceptionOrNull()
        }

        /** [target] no longer wants the stream; it stops once nobody does. */
        fun release(
            context: Context,
            target: Target,
        ) {
            if (!running) return
            val action = if (target == Target.TV) ACTION_RELEASE_TV else ACTION_RELEASE_LINK
            runCatching { context.startService(Intent(context, CastService::class.java).setAction(action)) }
                .onFailure { RingLog.note(TAG, "release refused: $it") }
        }
    }

    /** Who a stream is for: a TV through Cast, or a browser through "Show on a computer". */
    enum class Target { TV, LINK }
}
