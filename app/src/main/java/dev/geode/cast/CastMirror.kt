package dev.geode.cast

import android.os.Handler
import android.os.Looper
import dev.geode.engine.bridge.GeodeNative
import dev.geode.render.scene.SceneParams
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

/**
 * The stage's live look, held for the cast's fallback renderer, which draws only while the stage draws
 * nothing (the app in the background); while the stage draws, the stream carries its own frames
 * (core/viz/frame/CastTee). The fallback follows what the phone's visual last showed (owner, 2026-11-02),
 * a style only previewed in the Styles grid included, which the saved live state deliberately leaves out.
 *
 * The MilkDrop preset is mirrored too ([milkPath]): the cast renderer restores the process's
 * remembered preset only when it is built, so without it a .milk picked on the phone, or drawn by
 * Auto while the app is in the background, never reached the TV.
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

    /** The .milk the stage last asked for or showed; null before one, or once it was deleted. */
    @Volatile
    var milkPath: String? = null
        private set

    private val streamingFlow = MutableStateFlow(false)

    /**
     * True while a cast stream runs (TV or browser). The stream carries the stage's own frames (core/viz/frame/CastTee),
     * so the shell keeps the stage drawing while the app is open on another tab ([CastStream]).
     */
    val streaming: StateFlow<Boolean> = streamingFlow.asStateFlow()

    internal fun setStreaming(on: Boolean) {
        streamingFlow.value = on
    }

    /** What the shell last saw of the stage (the `GeodeNative.STAGE_*` bits, written by `ui/StageRun`); 0 with no Activity. */
    @Volatile
    var stageFlags: Int = 0

    /** Auto's step on a director moment, on the main thread; null while no Activity holds a session. */
    @Volatile
    var onDirectorMoment: (() -> Unit)? = null

    private val main = Handler(Looper.getMainLooper())

    /**
     * The cast's renderer reached a director moment (its render thread). Auto steps on it only while the
     * stage is not drawing, which is when the stage's own renderer stops counting them: without this a TV
     * cast from the background stayed on one style for the rest of the session. The rule is native
     * (core/viz/StageRun).
     */
    fun directorMoment() {
        val leads = GeodeNative.stageRun(stageFlags or GeodeNative.STAGE_CASTING) and GeodeNative.STAGE_CAST_LEADS_AUTO != 0
        if (leads) main.post { onDirectorMoment?.invoke() }
    }

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
    private var following: Job? = null

    /**
     * Follows [source] and [milk] (each preset the stage asks for or reports loaded) from now on;
     * called again (a new Activity), it replaces the old sources.
     */
    @Synchronized
    fun follow(
        source: Flow<Look>,
        milk: Flow<String?>,
    ) {
        following?.cancel()
        following =
            scope.launch {
                launch { milk.collect { milkPath = it } }
                source.collect { look = it }
            }
    }
}
