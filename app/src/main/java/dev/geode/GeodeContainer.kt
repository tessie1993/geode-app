package dev.geode

import android.content.Context
import dev.geode.data.GeodePrefsFiles
import dev.geode.ui.SharedPrefsUserDataRepository
import dev.geode.ui.UserDataRepository
import dev.geode.util.bestEffort
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import java.io.File

class GeodeContainer(
    context: Context,
) {
    val appScope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)

    val prefsFiles = GeodePrefsFiles(context)

    val userData: UserDataRepository = SharedPrefsUserDataRepository(prefsFiles.general, appScope)

    init {
        sweepStaleRenderScratch(context.applicationContext.cacheDir)
    }

    /**
     * Deletes render scratch files left behind by a render that never got to clean up after
     * itself — a crash, a foreground-service timeout, a low-memory kill, a force-stop.
     *
     * Every render deletes its own scratch on both the success and the failure path, so anything
     * still here belongs to a previous process. A whole-track AAC sidecar runs to tens of
     * megabytes and a loop reel to hundreds, and nothing else ever reclaims them, so without this
     * they accumulate for the life of the install.
     *
     * Safe to sweep wholesale because this runs while the container is being built, which
     * happens before anything can start a render in this process.
     */
    private fun sweepStaleRenderScratch(cacheDir: File) {
        appScope.launch(Dispatchers.IO) {
            bestEffort(TAG, "sweep stale render scratch") {
                cacheDir
                    .listFiles()
                    .orEmpty()
                    .filter { file -> file.isFile && isRenderScratch(file.name) }
                    .forEach { file -> file.delete() }
            }
        }
    }

    private companion object {
        const val TAG = "GeodeContainer"

        /** Kept in step with AudioTranscoder, LoopRender and StudioExporter. */
        val RENDER_SCRATCH_PREFIXES = listOf("geode_aac_", "geode_loop_", "studio-")

        fun isRenderScratch(name: String): Boolean = RENDER_SCRATCH_PREFIXES.any { name.startsWith(it) }
    }
}

val Context.geodeContainer: GeodeContainer
    get() = (applicationContext as GeodeApp).container
