package dev.geode.export

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.IBinder
import dev.geode.util.bestEffort
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.launch

class ExportService : Service() {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
    private var watcher: Job? = null

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        bestEffort(TAG, "startForegroundNotification(ExportRun.state.v...") { startForegroundNotification(ExportRun.state.value) }
        watcher =
            scope.launch {
                ExportRun.state.collectLatest { state ->
                    if (!state.running) {
                        stopSelf()
                        return@collectLatest
                    }
                    bestEffort(TAG, "startForegroundNotification(state)") { startForegroundNotification(state) }
                }
            }
    }

    override fun onStartCommand(
        intent: Intent?,
        flags: Int,
        startId: Int,
    ): Int {
        return START_NOT_STICKY
    }

    override fun onTimeout(
        startId: Int,
        fgsType: Int,
    ) {
        ExportRun.requestCancel()
        stopSelf()
    }

    override fun onDestroy() {
        watcher?.cancel()
        scope.cancel()
        super.onDestroy()
    }

    private fun startForegroundNotification(state: ExportRun.State) {
        val manager = getSystemService(NotificationManager::class.java)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            manager.createNotificationChannel(
                NotificationChannel(
                    CHANNEL_ID,
                    getString(dev.geode.R.string.export_channel_name),
                    NotificationManager.IMPORTANCE_LOW,
                ).apply {
                    description = getString(dev.geode.R.string.export_channel_description)
                    setShowBadge(false)
                },
            )
        }
        val open =
            PendingIntent.getActivity(
                this,
                0,
                Intent(this, dev.geode.ui.MainActivity::class.java),
                PendingIntent.FLAG_IMMUTABLE,
            )
        val builder =
            Notification
                .Builder(this, CHANNEL_ID)
                .setContentTitle(getString(dev.geode.R.string.export_notification_title))
                .setContentText(
                    listOfNotNull(
                        state.label.ifBlank { getString(dev.geode.R.string.export_notification_text) },
                        state.secondsRemaining?.let { RenderEta.describe(it) },
                    ).joinToString(" · "),
                )
                .setSmallIcon(dev.geode.R.drawable.ic_stat_capture)
                .setOngoing(true)
                .setContentIntent(open)
        val progress = state.progress
        if (progress == null) {
            builder.setProgress(0, 0, true)
        } else {
            builder.setProgress(PROGRESS_MAX, (progress * PROGRESS_MAX).toInt(), false)
        }
        val notification = builder.build()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
            startForeground(NOTIFICATION_ID, notification, foregroundType())
        } else {
            startForeground(NOTIFICATION_ID, notification)
        }
    }

    private fun foregroundType(): Int =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.VANILLA_ICE_CREAM) {
            ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROCESSING
        } else {
            ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC
        }

    companion object {
        private const val CHANNEL_ID = "geode-export"
        private const val NOTIFICATION_ID = 4711
        private const val PROGRESS_MAX = 1000

        fun start(context: Context) {
            runCatching {
                val intent = Intent(context, ExportService::class.java)
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                    context.startForegroundService(intent)
                } else {
                    context.startService(intent)
                }
            }
        }
    }
}

private const val TAG = "ExportService"
