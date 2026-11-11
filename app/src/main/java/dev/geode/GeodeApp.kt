package dev.geode

import android.app.Application
import dagger.hilt.android.HiltAndroidApp
import dev.geode.billing.ForegroundActivityTracker
import dev.geode.di.MemoryTrim
import java.io.File
import javax.inject.Inject

@HiltAndroidApp
class GeodeApp : Application() {
    val container: GeodeContainer by lazy { GeodeContainer(this) }

    @Inject lateinit var activityTracker: ForegroundActivityTracker

    override fun onCreate() {
        super.onCreate()
        // A weak reference to the resumed activity, for Play's purchase sheet; costs nothing.
        registerActivityLifecycleCallbacks(activityTracker)
        // The crash file first: everything after this (the stores below load libgeode and call into it)
        // is then caught by it.
        val previous = Thread.getDefaultUncaughtExceptionHandler()
        Thread.setDefaultUncaughtExceptionHandler { thread, throwable ->
            runCatching {
                File(filesDir, "crash-latest.txt").writeText(
                    "BeatRealms ${BuildConfig.VERSION_NAME}\n" +
                        "Thread: ${thread.name}\n\n" +
                        android.util.Log.getStackTraceString(throwable) +
                        "\n\n-- recent non-fatal failures --\n" +
                        RingLog.dump(),
                )
            }
            previous?.uncaughtException(thread, throwable)
        }
        if (BuildConfig.DEBUG) {
            // Logcat echo is a debug convenience; release keeps failures in the
            // ring buffer alone, where the crash report already includes them.
            RingLog.echo = { tag, line -> android.util.Log.w(tag, line) }
        }
        // Builds the container now: it starts the IO load of every settings repository, the prefs the
        // playback session reads while it is constructed on main, and the process-wide engine
        // configuration (the macro bank, the auto-director, Note & key reactions), which reaches native
        // as each read is back, before a surface (the app, the wallpaper, the dream, an export) needs it.
        container
    }

    /**
     * Memory pressure (#854). The one process serves the app, the wallpaper and the dream, so this one
     * callback covers all of them. Kotlin only forwards: see [MemoryTrim].
     */
    override fun onTrimMemory(level: Int) {
        super.onTrimMemory(level)
        MemoryTrim.onTrim(level)
    }
}
