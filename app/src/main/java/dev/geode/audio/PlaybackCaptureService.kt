package dev.geode.audio

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.media.projection.MediaProjection
import android.media.projection.MediaProjectionManager
import android.os.Build
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import dev.geode.util.bestEffort
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

class PlaybackCaptureService : Service() {
    private var projection: MediaProjection? = null

    private val projectionCallback =
        object : MediaProjection.Callback() {
            override fun onStop() {
                MediaProjectionHolder.publish(null)
                stopSelf()
            }
        }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(
        intent: Intent?,
        flags: Int,
        startId: Int,
    ): Int {
        bestEffort(TAG, "startForegroundNotification()") { startForegroundNotification() }
        if (intent?.action == ACTION_STOP) {
            stopSelf()
            return START_NOT_STICKY
        }
        val resultCode = intent?.getIntExtra(EXTRA_RESULT_CODE, 0) ?: 0
        val data = intent?.let { IntentCompat.projectionData(it) }
        if (resultCode == 0 || data == null) {
            MediaProjectionHolder.noteStartFailure()
            stopSelf()
            return START_NOT_STICKY
        }
        val manager = getSystemService(MediaProjectionManager::class.java)
        val mp =
            runCatching { manager.getMediaProjection(resultCode, data) }.getOrNull()
        if (mp == null) {
            MediaProjectionHolder.noteStartFailure()
            stopSelf()
            return START_NOT_STICKY
        }
        projection?.let {
            bestEffort(TAG, "it.unregisterCallback(projectionCallback)") { it.unregisterCallback(projectionCallback) }
            bestEffort(TAG, "it.stop()") { it.stop() }
        }
        mp.registerCallback(projectionCallback, Handler(Looper.getMainLooper()))
        projection = mp
        MediaProjectionHolder.publish(mp)
        return START_NOT_STICKY
    }

    override fun onDestroy() {
        MediaProjectionHolder.publish(null)
        projection?.let {
            bestEffort(TAG, "it.unregisterCallback(projectionCallback)") { it.unregisterCallback(projectionCallback) }
            bestEffort(TAG, "it.stop()") { it.stop() }
        }
        projection = null
        super.onDestroy()
    }

    private fun startForegroundNotification() {
        val manager = getSystemService(NotificationManager::class.java)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            manager.createNotificationChannel(
                NotificationChannel(
                    CHANNEL_ID,
                    getString(dev.geode.R.string.capture_channel_name),
                    NotificationManager.IMPORTANCE_LOW,
                ).apply {
                    description = getString(dev.geode.R.string.capture_channel_description)
                    setShowBadge(false)
                },
            )
        }
        val open =
            android.app.PendingIntent.getActivity(
                this,
                0,
                Intent(this, dev.geode.ui.MainActivity::class.java),
                android.app.PendingIntent.FLAG_IMMUTABLE,
            )
        val stop =
            android.app.PendingIntent.getService(
                this,
                1,
                Intent(this, PlaybackCaptureService::class.java).setAction(ACTION_STOP),
                android.app.PendingIntent.FLAG_IMMUTABLE,
            )
        val notification =
            Notification
                .Builder(this, CHANNEL_ID)
                .setContentTitle(getString(dev.geode.R.string.capture_notification_title))
                .setContentText(getString(dev.geode.R.string.capture_notification_text))
                .setSmallIcon(dev.geode.R.drawable.ic_stat_capture)
                .setOngoing(true)
                .setContentIntent(open)
                .addAction(
                    Notification.Action
                        .Builder(null, getString(dev.geode.R.string.capture_notification_stop), stop)
                        .build(),
                ).build()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
            startForeground(NOTIFICATION_ID, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION)
        } else {
            startForeground(NOTIFICATION_ID, notification)
        }
    }

    private object IntentCompat {
        @Suppress("DEPRECATION")
        fun projectionData(intent: Intent): Intent? =
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                intent.getParcelableExtra(EXTRA_RESULT_DATA, Intent::class.java)
            } else {
                intent.getParcelableExtra(EXTRA_RESULT_DATA)
            }
    }

    companion object {
        private const val CHANNEL_ID = "geode-capture"
        private const val NOTIFICATION_ID = 42
        private const val ACTION_STOP = "dev.geode.STOP_CAPTURE"
        private const val EXTRA_RESULT_CODE = "resultCode"
        private const val EXTRA_RESULT_DATA = "resultData"

        fun start(
            context: Context,
            resultCode: Int,
            data: Intent,
        ) {
            if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) return
            val intent =
                Intent(context, PlaybackCaptureService::class.java)
                    .putExtra(EXTRA_RESULT_CODE, resultCode)
                    .putExtra(EXTRA_RESULT_DATA, data)
            context.startForegroundService(intent)
        }

        fun stop(context: Context) {
            context.stopService(Intent(context, PlaybackCaptureService::class.java))
        }
    }
}

object MediaProjectionHolder {
    private val _projection = MutableStateFlow<MediaProjection?>(null)

    val projection: StateFlow<MediaProjection?> = _projection

    private val _startFailures = MutableStateFlow(0)

    val startFailures: StateFlow<Int> = _startFailures

    fun publish(projection: MediaProjection?) {
        _projection.value = projection
    }

    fun noteStartFailure() {
        _startFailures.value += 1
    }
}

private const val TAG = "PlaybackCaptureService"
