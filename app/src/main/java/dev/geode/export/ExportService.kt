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
import dev.geode.appLaunchIntent
import dev.geode.util.bestEffort
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.conflate
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch

class ExportService : Service() {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
    private var watcher: Job? = null

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        bestEffort(TAG, "startForeground") { startForegroundNotification(ExportRun.state.value) }
        // ExportRun publishes every rendered frame. The notification is re-posted only when what it
        // shows changes (progress in tenths of a percent, the ETA, the label), and at most once a
        // second: conflate keeps the newest state through the pause (#379).
        watcher =
            scope.launch {
                ExportRun.state
                    .map { it.copy(progress = it.progress?.let { p -> (p * PROGRESS_MAX).toInt() / PROGRESS_MAX.toFloat() }) }
                    .distinctUntilChanged()
                    .conflate()
                    .collect { state ->
                        if (!state.running) return@collect stopSelf()
                        bestEffort(TAG, "notify") { getSystemService(NotificationManager::class.java).notify(NOTIFICATION_ID, build(state)) }
                        delay(UPDATE_MS)
                    }
            }
    }

    override fun onStartCommand(
        intent: Intent?,
        flags: Int,
        startId: Int,
    ): Int = START_NOT_STICKY

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

    /** Once, in [onCreate]: the channel and the foreground start. Updates go through [build] and notify. */
    private fun startForegroundNotification(state: ExportRun.State) {
        getSystemService(NotificationManager::class.java).createNotificationChannel(
            NotificationChannel(
                CHANNEL_ID,
                getString(dev.geode.R.string.export_channel_name),
                NotificationManager.IMPORTANCE_LOW,
            ).apply {
                description = getString(dev.geode.R.string.export_channel_description)
                setShowBadge(false)
            },
        )
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
            startForeground(NOTIFICATION_ID, build(state), foregroundType())
        } else {
            startForeground(NOTIFICATION_ID, build(state))
        }
    }

    private fun build(state: ExportRun.State): Notification {
        val open = PendingIntent.getActivity(this, 0, appLaunchIntent(), PendingIntent.FLAG_IMMUTABLE)
        val builder =
            Notification
                .Builder(this, CHANNEL_ID)
                .setContentTitle(getString(dev.geode.R.string.export_notification_title))
                .setContentText(
                    listOfNotNull(
                        state.label.ifBlank { getString(dev.geode.R.string.export_notification_text) },
                        state.secondsRemaining?.let { RenderEta.describe(resources, it) },
                    ).joinToString(" · "),
                ).setSmallIcon(dev.geode.R.drawable.ic_stat_capture)
                .setOngoing(true)
                .setContentIntent(open)
        val progress = state.progress
        return builder.setProgress(PROGRESS_MAX, ((progress ?: 0f) * PROGRESS_MAX).toInt(), progress == null).build()
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
        private const val UPDATE_MS = 1_000L

        fun start(context: Context) {
            runCatching { context.startForegroundService(Intent(context, ExportService::class.java)) }
        }
    }
}

private const val TAG = "ExportService"
