package dev.geode.widget

import android.content.Context
import androidx.glance.appwidget.updateAll
import dev.geode.geodeContainer
import kotlinx.coroutines.channels.BufferOverflow
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.onCompletion
import kotlinx.coroutines.flow.onStart
import kotlinx.coroutines.flow.updateAndGet
import kotlinx.coroutines.launch

/**
 * The widget's side of the style: what the app's session tells it (the style on screen) and what
 * its style buttons ask of the session (step to the previous or next style).
 *
 * The style itself is the session's: its order, the Pro skip and the store all live in `ui`,
 * which this package may not name (tools/layer-gate.sh). So the session drives this, not the
 * other way round:
 *
 * ```
 * scope.launch { WidgetStyles.steps().collect { step -> nextStageStyle(…, step) { locked }?.let(::selectScene) } }
 * scope.launch { chosenSceneIdFlow.collect { WidgetStyles.publish(context, it) } }
 * ```
 *
 * While nobody collects [steps] (no session: the app is closed, or the process was rebuilt for
 * the widget alone) [listening] is false and the style buttons open the app instead of doing
 * nothing; a step that arrives anyway (a widget drawn while a session was alive, tapped after
 * it ended) redraws the widget into that state rather than being lost silently.
 */
object WidgetStyles {
    private val requests = MutableSharedFlow<Int>(extraBufferCapacity = 8, onBufferOverflow = BufferOverflow.DROP_OLDEST)
    private val collectors = MutableStateFlow(0)
    private val _listening = MutableStateFlow(false)

    /** True while a session collects [steps]; the widget then draws working style buttons. */
    val listening: StateFlow<Boolean> = _listening.asStateFlow()

    /**
     * The style steps the widget's buttons ask for: +1 next, -1 previous. Collect it for as long as
     * the session lives; the widget redraws when collection starts and when it ends.
     */
    fun steps(context: Context): Flow<Int> {
        val appContext = context.applicationContext
        return requests
            .onStart { setCollectors(appContext, +1) }
            .onCompletion { setCollectors(appContext, -1) }
    }

    /** The style now on screen (the chosen one, not a preview); the widget's visual and label follow it. */
    fun publish(
        context: Context,
        styleId: String,
    ) {
        val appContext = context.applicationContext
        if (WidgetState.load(appContext).styleId == styleId) return
        WidgetState.update(appContext) { it.copy(styleId = styleId) }
        redraw(appContext)
    }

    /** A style button's tap. False when no session took it, so the caller can say so. */
    internal fun request(step: Int): Boolean = _listening.value && requests.tryEmit(step)

    private fun setCollectors(
        context: Context,
        delta: Int,
    ) {
        val now = collectors.updateAndGet { it + delta }
        if (_listening.value != now > 0) {
            _listening.value = now > 0
            redraw(context)
        }
    }

    private fun redraw(context: Context) {
        // The process scope: a session ending cancels its own scope, and with it any redraw
        // launched there, which would leave working style buttons on a widget with no session.
        context.geodeContainer.appScope.launch { NowPlayingWidget().updateAll(context) }
    }
}
