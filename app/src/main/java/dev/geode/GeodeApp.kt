package dev.geode

import android.app.Application
import dagger.hilt.android.HiltAndroidApp
import dev.geode.billing.ForegroundActivityTracker
import dev.geode.data.AutoDirectorStore
import dev.geode.data.MacroStore
import dev.geode.data.NoteResponseStore
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
        // The macro bank is process-wide in the engine: hand the saved bank over before any
        // surface (the app, the wallpaper, the dream, an export) builds a renderer.
        MacroStore.get(this)
        // The auto-director's switch is process-wide too, and so is Note & key reactions'.
        AutoDirectorStore.get(this)
        NoteResponseStore.get(this)
        // Builds the container now: its init starts the IO load of the prefs the playback session
        // reads while it is constructed on main. A process the playback service starts alone never
        // touched the lazy container, so its session parsed those files on the main thread.
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
