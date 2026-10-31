package dev.geode.render

import android.content.Context
import dev.geode.render.scene.MilkStarterPack
import java.io.File

/**
 * The MilkDrop preset on screen, kept once for the whole process and saved across restarts.
 *
 * A native renderer does not live as long as the preset should. [VisualizerView] destroys its
 * handle on every detach (a tab change, the live view collapsing, PiP); a rotation or any other
 * activity rebuild makes a new view and renderer; the wallpaper, the dream and export build their
 * own; process death drops them all. Every one of them starts on projectM's idle preset unless it
 * is told otherwise, and that idle "M" logo is the reset this exists to prevent: every
 * [VisualizerRenderer] hands [restorePath] to each new handle before its first frame (through
 * `NativeViz.onCreated`), and export snapshots it. One memory for every surface is the user's
 * call: the wallpaper and the dream show the preset the app last showed, and a preset the
 * wallpaper moves on to is the one the app comes back on.
 *
 * Only a load the native side has reported counts as shown, and only that is saved: a request
 * that never reports was either still in flight or failed to compile, and a failed one would
 * only bring back black, so it is used just while nothing has loaded yet. A saved path whose
 * file has gone (deleted, or an old build's `milk-builtin` copy) is skipped. With nothing saved,
 * a first launch starts on one of the bundled presets ([FIRST_RUN_PRESET]), never the idle one.
 */
class MilkPresetMemory internal constructor(
    private val store: Store,
) {
    /** Where the shown preset is kept; [shared] saves it in [PREFS_FILE], the tests in memory. */
    internal interface Store {
        fun read(): String?

        fun write(path: String?)

        fun usable(path: String): Boolean

        /** The preset a first launch starts on, or null when there is none to give. */
        fun firstRun(): String?
    }

    @Volatile
    private var requested: String? = null

    @Volatile
    private var shown: String? = store.read()

    /** The last preset the native side reported, still on disk; null when none has been shown. */
    val lastShown: String? get() = shown?.takeIf(store::usable)

    /** What a new native renderer must start on; null only when not even a bundled preset is there. */
    val restorePath: String? get() = lastShown ?: requested?.takeIf(store::usable) ?: store.firstRun()

    /** The UI asked for [path]; any thread. */
    fun request(path: String) {
        requested = path
    }

    /** The native side compiled [path], whether asked for here or by auto-advance; GL thread. */
    fun loaded(path: String) {
        if (shown == path) return
        shown = path
        store.write(path)
    }

    /** [paths] were deleted from the library; a restore must not ask for them again. */
    fun forget(paths: Collection<String>) {
        if (requested in paths) requested = null
        if (shown in paths) {
            shown = null
            store.write(null)
        }
    }

    companion object {
        /** `GeodePrefsFiles.VIZ`: the key predates this class, so existing installs keep their preset. */
        const val PREFS_FILE = "geode-viz"
        const val KEY = "milk_path"
        const val FIRST_RUN_PRESET = "geode_bloom.milk"

        @Volatile
        private var instance: MilkPresetMemory? = null

        /** The process's one memory; every renderer, the app's state and export read this one. */
        fun shared(context: Context): MilkPresetMemory =
            instance ?: synchronized(this) {
                instance ?: MilkPresetMemory(PrefsStore(context.applicationContext)).also { instance = it }
            }
    }
}

private class PrefsStore(
    private val context: Context,
) : MilkPresetMemory.Store {
    private val prefs by lazy { context.getSharedPreferences(MilkPresetMemory.PREFS_FILE, Context.MODE_PRIVATE) }

    override fun read(): String? = prefs.getString(MilkPresetMemory.KEY, null)

    override fun write(path: String?) {
        val edit = prefs.edit()
        if (path == null) edit.remove(MilkPresetMemory.KEY) else edit.putString(MilkPresetMemory.KEY, path)
        edit.apply()
    }

    override fun usable(path: String): Boolean = File(path).isFile

    // The starter pack is copied once per install (it keeps a marker), so this is a file check
    // after the first launch; it only runs while nothing has ever been shown. Synchronized: the app's,
    // the wallpaper's and the dream's GL threads can all ask at once, and must not copy the same file.
    @Synchronized
    override fun firstRun(): String? {
        MilkStarterPack.install(context)
        return File(File(context.filesDir, "milk"), MilkPresetMemory.FIRST_RUN_PRESET)
            .takeIf { it.isFile }
            ?.absolutePath
    }
}
