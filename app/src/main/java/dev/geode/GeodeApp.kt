package dev.geode

import android.app.Application
import dagger.hilt.android.HiltAndroidApp
import java.io.File

@HiltAndroidApp
class GeodeApp : Application() {
    val container: GeodeContainer by lazy { GeodeContainer(this) }

    override fun onCreate() {
        super.onCreate()
        if (BuildConfig.DEBUG) {
            // Logcat echo is a debug convenience; release keeps failures in the
            // ring buffer alone, where the crash report already includes them.
            RingLog.echo = { tag, line -> android.util.Log.w(tag, line) }
            android.os.StrictMode.setThreadPolicy(
                android.os.StrictMode.ThreadPolicy
                    .Builder()
                    .detectDiskReads()
                    .detectDiskWrites()
                    .detectNetwork()
                    .penaltyLog()
                    .build(),
            )
        }
        val previous = Thread.getDefaultUncaughtExceptionHandler()
        Thread.setDefaultUncaughtExceptionHandler { thread, throwable ->
            runCatching {
                File(filesDir, "crash-latest.txt").writeText(
                    "Geode ${BuildConfig.VERSION_NAME}\n" +
                        "Thread: ${thread.name}\n\n" +
                        android.util.Log.getStackTraceString(throwable) +
                        "\n\n-- recent non-fatal failures --\n" +
                        RingLog.dump(),
                )
            }
            previous?.uncaughtException(thread, throwable)
        }
    }
}
