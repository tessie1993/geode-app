package dev.geode

import android.app.Application
import dagger.hilt.android.HiltAndroidApp
import dev.geode.billing.ForegroundActivityTracker
import dev.geode.data.AutoDirectorStore
import dev.geode.data.MacroStore
import dev.geode.data.NoteResponseStore
import dev.geode.data.PaletteStore
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
                    "MuzViz ${BuildConfig.VERSION_NAME}\n" +
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
        // Customize › Macros are process-wide in the engine: hand the saved knobs over before any
        // surface (the app, the wallpaper, the dream, an export) builds a renderer.
        MacroStore.get(this)
        // The auto-director's switch is process-wide too, and so is Note & key reactions'.
        AutoDirectorStore.get(this)
        NoteResponseStore.get(this)
        // The saved gradient palettes and the sequence, for every renderer a preset or an export builds.
        runCatching { PaletteStore.publishSaved(this) }
    }
}
