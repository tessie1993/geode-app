package dev.geode.ui

import android.app.Presentation
import android.content.Context
import android.hardware.display.DisplayManager
import android.os.Bundle
import android.view.Display
import android.view.ViewGroup
import android.view.WindowManager
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalView
import androidx.lifecycle.DefaultLifecycleObserver
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.engine.bridge.GeodeNative
import dev.geode.pro.PaywallTrigger
import dev.geode.pro.ProFeature
import dev.geode.render.VisualizerView
import dev.geode.ui.pro.PaywallViewModel

/**
 * The connected displays (HDMI, USB-C DisplayPort, a wireless display) as the shell sees them. Which one
 * the stage shows on, and whether, is native (core/viz/StageRun); this keeps what Android reports, the
 * user's pick for this run and how often each display refused or dropped the presentation.
 */
internal object ConnectedDisplays {
    /** The display the stage would show on, whether or not it does; null with none that qualifies. */
    var available by mutableStateOf<Display?>(null)
        private set

    /** Every display that qualifies, for the picker. */
    var choices by mutableStateOf<List<Display>>(emptyList())
        private set

    /** True while a presentation of the stage is up. */
    var presenting by mutableStateOf(false)
        internal set

    /** Bumped when a pick or a failure needs a fresh look; [ConnectedDisplayWatch] refreshes on it. */
    var revision by mutableIntStateOf(0)
        private set

    private var preferredId = Display.INVALID_DISPLAY
    private val failures = mutableMapOf<Int, Int>()

    fun choose(displayId: Int) {
        preferredId = displayId
        revision++
    }

    /** Whether the stage goes to [available]: the switch, Cast's entitlement and a display to go to. */
    fun target(
        wanted: Boolean,
        entitled: Boolean,
    ): Display? {
        val display = available ?: return null
        var flags = GeodeNative.STAGE_PRESENT_FOUND
        if (wanted) flags = flags or GeodeNative.STAGE_PRESENT_WANTED
        if (entitled) flags = flags or GeodeNative.STAGE_PRESENT_ENTITLED
        return display.takeIf { GeodeNative.stagePresents(flags) }
    }

    fun failed(displayId: Int) {
        failures[displayId] = (failures[displayId] ?: 0) + 1
        revision++
    }

    fun changed(displayId: Int) {
        failures.remove(displayId)
    }

    fun clearFailures() {
        failures.clear()
    }

    fun forget() {
        available = null
        choices = emptyList()
    }

    fun refresh(context: Context) {
        val manager = context.getSystemService(DisplayManager::class.java) ?: return
        val displays = manager.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION).toList()
        failures.keys.retainAll(displays.map { it.displayId }.toSet())
        val hostId = runCatching { context.display.displayId }.getOrDefault(Display.DEFAULT_DISPLAY)
        val flags = IntArray(displays.size) { flagsOf(displays[it], hostId) }
        val picked = GeodeNative.stagePickDisplay(IntArray(displays.size) { displays[it].displayId }, flags, preferredId)
        available = displays.getOrNull(picked)
        choices = displays.filterIndexed { i, _ -> GeodeNative.stageDisplayEligible(flags[i]) }
    }

    private fun flagsOf(
        display: Display,
        hostId: Int,
    ): Int {
        var flags = (failures[display.displayId] ?: 0).coerceAtMost(MAX_FAILURE_COUNT) shl GeodeNative.STAGE_DISPLAY_FAILURE_SHIFT
        if (display.flags and Display.FLAG_PRESENTATION != 0) flags = flags or GeodeNative.STAGE_DISPLAY_PRESENTATION
        if (display.displayId == Display.DEFAULT_DISPLAY) flags = flags or GeodeNative.STAGE_DISPLAY_DEFAULT
        if (display.displayId == hostId) flags = flags or GeodeNative.STAGE_DISPLAY_HOST
        if (display.state == Display.STATE_ON) flags = flags or GeodeNative.STAGE_DISPLAY_ON
        return flags
    }

    /** The failure count's field is eight bits wide. */
    private const val MAX_FAILURE_COUNT = 0xFF
}

/**
 * Keeps [ConnectedDisplays] current from AppRoot: a display added, removed or changed, the Activity moved to
 * another display (a configuration change), and a fresh start forgiving earlier refusals.
 */
@Composable
internal fun ConnectedDisplayWatch() {
    val context = LocalContext.current
    val configuration = LocalConfiguration.current
    val lifecycle = LocalLifecycleOwner.current.lifecycle
    val revision = ConnectedDisplays.revision
    DisposableEffect(context, configuration, revision) {
        ConnectedDisplays.refresh(context)
        onDispose {}
    }
    DisposableEffect(context, lifecycle) {
        val manager = context.getSystemService(DisplayManager::class.java)
        val listener =
            object : DisplayManager.DisplayListener {
                override fun onDisplayAdded(displayId: Int) = ConnectedDisplays.refresh(context)

                override fun onDisplayRemoved(displayId: Int) = ConnectedDisplays.refresh(context)

                override fun onDisplayChanged(displayId: Int) {
                    ConnectedDisplays.changed(displayId)
                    ConnectedDisplays.refresh(context)
                }
            }
        val starts =
            object : DefaultLifecycleObserver {
                override fun onStart(owner: LifecycleOwner) {
                    ConnectedDisplays.clearFailures()
                    ConnectedDisplays.refresh(context)
                }
            }
        manager?.registerDisplayListener(listener, null)
        lifecycle.addObserver(starts)
        onDispose {
            manager?.unregisterDisplayListener(listener)
            lifecycle.removeObserver(starts)
            ConnectedDisplays.forget()
        }
    }
}

/**
 * The stage on [display] alone, in a Presentation sized to that display's own mode, while the phone keeps
 * the controls. Android drops a presentation when the display's metrics change or its rules no longer allow
 * it (the docs' occlusion rules); a drop while the app is started counts as a failure and is retried, one
 * while stopped is retried at the next start, and native stops picking a display that keeps refusing, so the
 * visual comes back to the phone. Both screens are kept on while it shows: a stopped Activity hides its
 * presentation.
 */
@Composable
fun SecondScreenCanvas(
    display: Display,
    view: VisualizerView,
) {
    val context = LocalContext.current
    val host = LocalView.current
    val lifecycle = LocalLifecycleOwner.current.lifecycle
    var attempt by remember(display) { mutableIntStateOf(0) }
    DisposableEffect(display, view, attempt) {
        var ours = false
        var retryAtStart: DefaultLifecycleObserver? = null
        val presentation =
            object : Presentation(context, display) {
                override fun onCreate(savedInstanceState: Bundle?) {
                    super.onCreate(savedInstanceState)
                    (view.parent as? ViewGroup)?.removeView(view)
                    setContentView(view)
                    window?.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                }
            }
        presentation.setOnDismissListener {
            if (ours) return@setOnDismissListener
            ConnectedDisplays.presenting = false
            (view.parent as? ViewGroup)?.removeView(view)
            if (lifecycle.currentState.isAtLeast(Lifecycle.State.STARTED)) {
                ConnectedDisplays.failed(display.displayId)
                attempt++
            } else {
                val retry =
                    object : DefaultLifecycleObserver {
                        override fun onStart(owner: LifecycleOwner) {
                            owner.lifecycle.removeObserver(this)
                            attempt++
                        }
                    }
                retryAtStart = retry
                lifecycle.addObserver(retry)
            }
        }
        val shown = runCatching { presentation.show() }.isSuccess
        if (shown) {
            ConnectedDisplays.presenting = true
            host.keepScreenOn = true
        } else {
            // show() runs onCreate before the window is refused, so the view may already have moved.
            (view.parent as? ViewGroup)?.removeView(view)
            ConnectedDisplays.failed(display.displayId)
            attempt++
        }
        onDispose {
            ours = true
            retryAtStart?.let(lifecycle::removeObserver)
            host.keepScreenOn = false
            ConnectedDisplays.presenting = false
            if (shown) runCatching { presentation.dismiss() }
            (view.parent as? ViewGroup)?.removeView(view)
        }
    }
}

/**
 * What a Connected display control shows and does, the same on the stage icon and both settings pages:
 * [display] is null with nothing connected, [on] is the user's switch as it applies, and [toggle] opens the
 * paywall without Cast's entitlement (the gate the second screen shares with Cast).
 */
internal class ConnectedDisplayControl(
    val display: Display?,
    val on: Boolean,
    val showing: Boolean,
    val locked: Boolean,
    val toggle: () -> Unit,
) {
    /** The TalkBack label of the stage icon. */
    val actionRes: Int
        get() =
            when {
                locked -> R.string.display_action_locked
                on -> R.string.display_action_stop
                else -> R.string.display_action_show
            }

    /** One line of state under the switch. */
    val stateRes: Int
        get() =
            when {
                locked -> R.string.display_state_locked
                showing -> R.string.display_state_showing
                on -> R.string.display_state_waiting
                else -> R.string.display_state_ready
            }
}

@Composable
internal fun rememberConnectedDisplayControl(): ConnectedDisplayControl {
    val settings: SettingsViewModel = geodeViewModel()
    val gui by settings.guiPrefs.collectAsStateWithLifecycle()
    val paywall: PaywallViewModel = geodeViewModel()
    val gate by paywall.gate.collectAsStateWithLifecycle()
    val locked = !gate.allows(ProFeature.CAST)
    return ConnectedDisplayControl(
        display = ConnectedDisplays.available,
        on = gui.secondScreen && !locked,
        showing = ConnectedDisplays.presenting,
        locked = locked,
    ) {
        if (locked) {
            paywall.open(PaywallTrigger.Feature(ProFeature.CAST))
        } else {
            settings.setGuiPrefs(gui.copy(secondScreen = !gui.secondScreen))
        }
    }
}
