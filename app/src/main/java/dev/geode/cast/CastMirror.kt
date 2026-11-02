package dev.geode.cast

import dev.geode.render.scene.SceneParams
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.launch

/**
 * The stage's live look, held for the cast renderer: the TV mirrors exactly what the phone's visual
 * shows (owner, 2026-11-02), a style only previewed in the Styles grid included, which the saved
 * live state deliberately leaves out.
 *
 * The UI feeds it ([follow], from `ui/cast/CastButton`); `cast/` can't read the UI's state itself
 * (the layer rule keeps the UI package out of every other package). It follows that flow for the
 * life of the process, so it keeps mirroring with the app in the background. Until something has
 * fed it, [look] is null and [CastStream] falls back to the saved live state.
 */
object CastMirror {
    /** A style and its params, as the stage draws them. */
    data class Look(
        val sceneId: String,
        val params: SceneParams,
    )

    @Volatile
    var look: Look? = null
        private set

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
    private var following: Job? = null

    /** Follows [source] from now on; called again (a new Activity), it replaces the old source. */
    @Synchronized
    fun follow(source: Flow<Look>) {
        following?.cancel()
        following = scope.launch { source.collect { look = it } }
    }
}
