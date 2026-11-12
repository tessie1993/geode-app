// Named for its settings pages; BehaviourPrefsHolder is only the pages' copy of the prefs.
@file:Suppress("MatchingDeclarationName")

package dev.geode.ui

import android.Manifest
import android.app.WallpaperManager
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.provider.Settings
import android.view.WindowManager
import androidx.annotation.StringRes
import androidx.compose.foundation.layout.Column
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.data.BehaviourPrefs
import dev.geode.data.CanvasGesture
import dev.geode.data.GestureAction
import dev.geode.data.NotificationButton
import dev.geode.data.QueueEndAction
import dev.geode.data.StartScreen
import dev.geode.geodeContainer
import dev.geode.pro.PaywallTrigger
import dev.geode.pro.ProFeature
import dev.geode.ui.pro.PaywallViewModel
import dev.geode.ui.theme.rememberSystemReducedMotion
import dev.geode.wallpaper.VisualizerWallpaperService
import kotlinx.coroutines.launch

// The rows stored in [BehaviourPrefs] (`geode-prefs`, defaults the owner's), which Settings spreads
// over seven tabs since the regroup: Playback (queue end, shuffle and repeat), Output & devices
// (headphones, notification buttons), Displays & casting (the screen on in Visuals), Wallpaper &
// screen saver (the dream), Gestures & controls (the gesture map) and Look (where the app opens);
// plus the visual's other rows that are not one store's: safety and motion, touch, a second
// screen, the wallpaper button. Only options the app honours get a row (#337): a switch that does
// nothing is worse than no switch.

/** A change to [BehaviourPrefs], applied to the latest value by the repository. */
internal typealias BehaviourEdit = ((BehaviourPrefs) -> BehaviourPrefs) -> Unit

/** [BehaviourPrefs] as a page holds it: null until the first read (on IO) is back. */
internal class BehaviourPrefsHolder(
    val prefs: BehaviourPrefs?,
    val update: BehaviourEdit,
)

/**
 * The container's one [dev.geode.data.BehaviourRepository]: null until its load is back, then its
 * value, and each change applied to the latest value, so a row never writes back a stale copy over
 * the last screen recorded meanwhile or another page's change.
 */
@Composable
internal fun rememberBehaviourPrefs(): BehaviourPrefsHolder {
    val ctx = LocalContext.current
    val repository = remember { ctx.geodeContainer.behaviour }
    val scope = rememberCoroutineScope()
    val prefs by produceState<BehaviourPrefs?>(null, repository) {
        repository.loaded()
        repository.value.collect { value = it }
    }
    return BehaviourPrefsHolder(prefs) { edit -> scope.launch { repository.update(edit) } }
}

/** One [BehaviourPrefs] row by its key, whichever tab holds it. */
@Composable
internal fun BehaviourRow(
    row: SettingsRowKey,
    prefs: BehaviourPrefs,
    update: BehaviourEdit,
) {
    when (row) {
        SettingsRowKey.START_SCREEN ->
            SettingsChoiceRow(
                title = stringResource(R.string.settings_beh_start_screen),
                options = StartScreen.entries,
                selected = prefs.startScreen,
                label = { stringResource(startScreenLabel(it)) },
                onSelect = { screen -> update { it.copy(startScreen = screen) } },
            )
        SettingsRowKey.KEEP_SCREEN_ON_IN_VISUALS ->
            SettingsSwitchRow(
                title = stringResource(R.string.settings_beh_keep_screen_on),
                summary = stringResource(R.string.settings_beh_keep_screen_on_summary),
                checked = prefs.keepScreenOnVisuals,
                onCheckedChange = { on -> update { it.copy(keepScreenOnVisuals = on) } },
            )
        SettingsRowKey.HEADPHONES_AUTOPLAY ->
            SettingsSwitchRow(
                title = stringResource(R.string.settings_beh_headphones),
                summary = stringResource(R.string.settings_beh_headphones_summary),
                checked = prefs.startOnHeadphones,
                onCheckedChange = { on -> update { it.copy(startOnHeadphones = on) } },
            )
        SettingsRowKey.QUEUE_END -> QueueEndRow(prefs, update)
        SettingsRowKey.REMEMBER_SHUFFLE ->
            SettingsSwitchRow(
                title = stringResource(R.string.settings_beh_remember_shuffle),
                summary = stringResource(R.string.settings_beh_remember_shuffle_summary),
                checked = prefs.rememberShuffleRepeat,
                onCheckedChange = { on -> update { it.copy(rememberShuffleRepeat = on) } },
            )
        SettingsRowKey.GESTURE_MAP -> GestureMap(prefs, update)
        SettingsRowKey.NOTIFICATION_BUTTONS -> NotificationButtonsRow(prefs, update)
        SettingsRowKey.DREAM_BRIGHT ->
            SettingsSwitchRow(
                title = stringResource(R.string.settings_dream_bright),
                checked = prefs.dreamScreenBright,
                onCheckedChange = { on -> update { it.copy(dreamScreenBright = on) } },
            )
        SettingsRowKey.DREAM_CLOCK ->
            SettingsSwitchRow(
                title = stringResource(R.string.settings_dream_clock),
                checked = prefs.dreamShowClock,
                onCheckedChange = { on -> update { it.copy(dreamShowClock = on) } },
            )
        SettingsRowKey.DREAM_NOW_PLAYING ->
            SettingsSwitchRow(
                title = stringResource(R.string.settings_dream_now_playing),
                checked = prefs.dreamShowNowPlaying,
                onCheckedChange = { on -> update { it.copy(dreamShowNowPlaying = on) } },
            )
        SettingsRowKey.DREAM_OTHER_APPS -> DreamOtherAppsRow(prefs, update)
        else -> Unit
    }
}

/** Playback's "When the queue ends": stop, repeat it, or carry on with similar tracks. */
@Composable
private fun QueueEndRow(
    prefs: BehaviourPrefs,
    update: BehaviourEdit,
) {
    SettingsChoiceRow(
        title = stringResource(R.string.settings_beh_queue_end),
        options = QueueEndAction.entries,
        selected = prefs.queueEnd,
        label = {
            stringResource(
                when (it) {
                    QueueEndAction.STOP -> R.string.settings_beh_queue_end_stop
                    QueueEndAction.REPEAT -> R.string.settings_beh_queue_end_repeat
                    QueueEndAction.SIMILAR -> R.string.settings_beh_queue_end_similar
                },
            )
        },
        onSelect = { action -> update { it.copy(queueEnd = action) } },
    )
}

/**
 * Gestures & controls: one dropdown per gesture on the visual ([BehaviourPrefs.gestures], read by
 * `VisualGestures` the next time Visuals opens). Pinch and twist can only be given touch play: what
 * the pinch does to the canvas itself is the Touch section's switch.
 */
@Composable
private fun GestureMap(
    prefs: BehaviourPrefs,
    update: BehaviourEdit,
) {
    SettingsHint(stringResource(R.string.settings_gestures_hint))
    CanvasGesture.entries.forEach { gesture ->
        SettingsChoiceRow(
            title = stringResource(gestureLabel(gesture)),
            options = gestureChoices(gesture, prefs.actionFor(gesture)),
            selected = prefs.actionFor(gesture),
            label = { stringResource(gestureActionLabel(it)) },
            onSelect = { action -> update { it.copy(gestures = it.gestures + (gesture to action)) } },
        )
    }
}

/** Output & devices › Notification buttons: the extra buttons beside play, previous and next. */
@Composable
private fun NotificationButtonsRow(
    prefs: BehaviourPrefs,
    update: BehaviourEdit,
) {
    NotificationButton.entries.forEach { button ->
        SettingsSwitchRow(
            title =
                stringResource(
                    when (button) {
                        NotificationButton.SHUFFLE -> R.string.settings_beh_notif_shuffle
                        NotificationButton.REPEAT -> R.string.settings_beh_notif_repeat
                        NotificationButton.FAVOURITE -> R.string.settings_beh_notif_favourite
                    },
                ),
            checked = button in prefs.notificationButtons,
            onCheckedChange = { on ->
                update { it.copy(notificationButtons = NotificationButton.toggled(it.notificationButtons, button, on)) }
            },
        )
    }
    Text(
        stringResource(R.string.settings_beh_notif_room),
        style = MaterialTheme.typography.bodySmall,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
    )
}

/** Whether swiping home from the fullscreen visualizer during playback drops into PiP on its own. */
@Composable
internal fun PictureInPictureSection(viewModel: SettingsViewModel) {
    val general by viewModel.general.collectAsStateWithLifecycle()
    LabeledSwitch(
        stringResource(R.string.behavior_pip_auto_enter),
        checked = general.autoEnterPip,
        onCheckedChange = { viewModel.updateGeneral { s -> s.copy(autoEnterPip = it) } },
    )
    SettingsHint(stringResource(R.string.behavior_pip_auto_enter_hint))
}

/**
 * Photosensitivity & motion: the flash clamp, stated rather than tuned. It is unconditional in the
 * render path, so there is nothing to switch; the row says what it covers and what it does not.
 */
@Composable
internal fun FlashLimitsRow() {
    SettingsHint(stringResource(R.string.behavior_flash_guidance))
}

/**
 * Photosensitivity & motion: reduced motion follows Android's "Remove animations" alone (#336), so
 * this row says which way it is now and opens Android's page to change it.
 */
@Composable
internal fun ReducedMotionRow() {
    val context = LocalContext.current
    val reduced = rememberSystemReducedMotion()
    SettingsRow(
        title = stringResource(R.string.behavior_motion_system),
        summary = stringResource(if (reduced) R.string.behavior_motion_system_on else R.string.behavior_motion_system_off),
        onClick = {
            bestEffortStart(context, Intent(Settings.ACTION_ACCESSIBILITY_SETTINGS))
        },
    )
}

/**
 * Gestures & controls › Touch: the fingers smearing the picture (handed to native by
 * `EnginePlumbing`, drawn by `core/viz/TouchSmear`), with its strength while it is on.
 */
@Composable
internal fun TouchSmearRow(viewModel: SettingsViewModel) {
    val visuals by viewModel.visuals.collectAsStateWithLifecycle()
    Column {
        LabeledSwitch(
            stringResource(R.string.behavior_touch_smear),
            checked = visuals.touchSmear,
            onCheckedChange = { viewModel.updateVisuals { s -> s.copy(touchSmear = it) } },
        )
        SettingsHint(stringResource(R.string.behavior_touch_smear_hint))
        if (visuals.touchSmear) {
            CrystalSlider(
                label = stringResource(R.string.behavior_smear_strength, (visuals.touchSmearStrength * 100).toInt()),
                value = visuals.touchSmearStrength,
                onValueChange = { viewModel.updateVisuals { s -> s.copy(touchSmearStrength = it) } },
                valueRange = 0.2f..2f,
            )
        }
    }
}

/** Gestures & controls › Touch: pinch and twist moving Zoom and Rotation (`VisualsHub`). */
@Composable
internal fun TouchTransformRow(viewModel: SettingsViewModel) {
    val visuals by viewModel.visuals.collectAsStateWithLifecycle()
    Column {
        LabeledSwitch(
            stringResource(R.string.behavior_touch_transform),
            checked = visuals.touchTransform,
            onCheckedChange = { viewModel.updateVisuals { s -> s.copy(touchTransform = it) } },
        )
        SettingsHint(stringResource(R.string.behavior_touch_transform_hint))
    }
}

/** Displays & casting › Connected display: the visual on HDMI, USB-C or a wireless display (Pro, as Cast). */
@Composable
internal fun ConnectedDisplaySection() {
    val control = rememberConnectedDisplayControl()
    LabeledSwitch(
        stringResource(R.string.behavior_display_use),
        checked = control.on,
        onCheckedChange = { control.toggle() },
    )
    val display = control.display
    if (display != null) {
        SettingsHint(stringResource(control.stateRes, display.name))
        SettingsHint(stringResource(R.string.behavior_display_connected))
    } else {
        SettingsHint(stringResource(R.string.behavior_display_none))
    }
}

/**
 * Wallpaper & screen saver: set the live wallpaper (Pro). The dream's own switches beside it
 * ([BehaviourPrefs.dreamScreenBright], [BehaviourPrefs.dreamShowClock],
 * [BehaviourPrefs.dreamShowNowPlaying], [BehaviourPrefs.dreamHearOtherApps]) are [BehaviourRow]s,
 * read by `wallpaper/VisualizerDreamService` when it starts.
 */
@Composable
internal fun LiveWallpaperRow() {
    val ctx = LocalContext.current
    val paywall: PaywallViewModel = geodeViewModel()
    val gate by paywall.gate.collectAsStateWithLifecycle()
    SettingsHint(stringResource(R.string.behavior_wallpaper_hint))
    CrystalButton(onClick = {
        if (!gate.allows(ProFeature.LIVE_WALLPAPER)) {
            paywall.open(PaywallTrigger.Feature(ProFeature.LIVE_WALLPAPER))
            return@CrystalButton
        }
        val direct =
            Intent(WallpaperManager.ACTION_CHANGE_LIVE_WALLPAPER).putExtra(
                WallpaperManager.EXTRA_LIVE_WALLPAPER_COMPONENT,
                ComponentName(ctx, VisualizerWallpaperService::class.java),
            )
        val ok = runCatching { ctx.startActivity(direct) }.isSuccess
        if (!ok) bestEffortStart(ctx, Intent(Intent.ACTION_SET_WALLPAPER))
    }) { Text(stringResource(R.string.behavior_wallpaper_button)) }
}

/**
 * The screen saver reacting to other apps' sound ([BehaviourPrefs.dreamHearOtherApps]), as the
 * wallpaper's "React to any app" does: turning it on asks for the permission the output-mix
 * capture needs in place, and while it is on without it one button asks again (or opens Android's
 * settings once it has stopped asking), so a refusal is never a switch that silently does nothing.
 */
@Composable
private fun DreamOtherAppsRow(
    prefs: BehaviourPrefs,
    update: BehaviourEdit,
) {
    val permission = rememberPermissionRequest(Manifest.permission.RECORD_AUDIO)
    SettingsSwitchRow(
        title = stringResource(R.string.settings_dream_other_apps),
        summary = stringResource(R.string.settings_dream_other_apps_hint),
        checked = prefs.dreamHearOtherApps,
        onCheckedChange = { on ->
            update { it.copy(dreamHearOtherApps = on) }
            if (on && !permission.granted) permission.request()
        },
    )
    if (prefs.dreamHearOtherApps && !permission.granted) {
        CrystalButton(onClick = permission::request) { Text(stringResource(R.string.wallpaper_options_open_settings)) }
    }
}

/**
 * Holds `FLAG_KEEP_SCREEN_ON` on the Activity's window while this is composed and [enabled]
 * (Settings › Displays & casting › Keep the screen on in Visuals, #337); `VisualsHub` composes it,
 * so the
 * flag is held only while Visuals shows and is cleared the moment it leaves. Player's own
 * keep-screen-on (`PlayerPrefs.keepScreenOn`, on the visualizer view) is a separate choice.
 */
@Composable
internal fun KeepScreenOnWhileShown(enabled: Boolean) {
    val context = LocalContext.current
    DisposableEffect(context, enabled) {
        val window = context.findMainActivity()?.window
        if (enabled) window?.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        onDispose {
            if (enabled) window?.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        }
    }
}

/** Starts a system screen, quietly doing nothing where the device has none for it. */
private fun bestEffortStart(
    context: Context,
    intent: Intent,
) {
    runCatching { context.startActivity(intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) }
}

/**
 * The actions a gesture can take: a sideways swipe steps through something (a style, a preset or
 * the playlist's look); a two-finger drag and a pinch move over the visual; the taps, the other swipes, the
 * hold and the swirl each open or do one thing. Its current action always stays on the list, so a
 * choice made elsewhere is never hidden.
 */
private fun gestureChoices(
    gesture: CanvasGesture,
    current: GestureAction,
): List<GestureAction> {
    val suited =
        when (gesture) {
            CanvasGesture.SWIPE_SIDEWAYS -> listOf(GestureAction.STEP_STYLE, GestureAction.STEP_PRESET, GestureAction.STEP_PLAYLIST)
            CanvasGesture.TWO_FINGER_DRAG, CanvasGesture.PINCH_TWIST -> listOf(GestureAction.TOUCH_PLAY)
            else -> ONE_SHOT_ACTIONS
        }
    return (suited + GestureAction.OFF).let { if (current in it) it else listOf(current) + it }
}

private val ONE_SHOT_ACTIONS =
    listOf(
        GestureAction.SHOW_MINI_PLAYER,
        GestureAction.TOGGLE_SHEET,
        GestureAction.MACROS,
        GestureAction.RECORD,
        GestureAction.RANDOMIZE_ALL,
        GestureAction.PLAY_PAUSE,
    )

@StringRes
private fun gestureLabel(gesture: CanvasGesture): Int =
    when (gesture) {
        CanvasGesture.TAP_BOTTOM -> R.string.settings_gesture_tap_bottom
        CanvasGesture.TAP_TOP -> R.string.settings_gesture_tap_top
        CanvasGesture.DOUBLE_TAP -> R.string.settings_gesture_double_tap
        CanvasGesture.SWIPE_SIDEWAYS -> R.string.settings_gesture_swipe_sideways
        CanvasGesture.SWIPE_UP -> R.string.settings_gesture_swipe_up
        CanvasGesture.SWIPE_DOWN -> R.string.settings_gesture_swipe_down
        CanvasGesture.LONG_PRESS -> R.string.settings_gesture_long_press
        CanvasGesture.SWIRL -> R.string.settings_gesture_swirl
        CanvasGesture.TWO_FINGER_DRAG -> R.string.settings_gesture_two_finger_drag
        CanvasGesture.PINCH_TWIST -> R.string.settings_gesture_pinch
    }

@StringRes
private fun gestureActionLabel(action: GestureAction): Int =
    when (action) {
        GestureAction.OFF -> R.string.settings_beh_nothing
        GestureAction.SHOW_MINI_PLAYER -> R.string.settings_gesture_action_mini_player
        GestureAction.QUICK_SETTINGS -> R.string.settings_gesture_action_quick_settings
        GestureAction.TOGGLE_SHEET -> R.string.settings_gesture_action_toggle_sheet
        GestureAction.STEP_STYLE -> R.string.settings_gesture_action_step_style
        GestureAction.STEP_PRESET -> R.string.settings_gesture_action_step_preset
        GestureAction.STEP_PLAYLIST -> R.string.settings_gesture_action_step_playlist
        GestureAction.MACROS -> R.string.settings_gesture_action_macros
        GestureAction.RECORD -> R.string.settings_gesture_action_record
        GestureAction.RANDOMIZE_ALL -> R.string.settings_gesture_action_randomize
        GestureAction.TOUCH_PLAY -> R.string.settings_gesture_action_touch_play
        GestureAction.PLAY_PAUSE -> R.string.settings_gesture_action_play_pause
    }

@StringRes
private fun startScreenLabel(screen: StartScreen): Int =
    when (screen) {
        StartScreen.LAST_USED -> R.string.settings_beh_start_last
        StartScreen.PLAYER -> R.string.nav_player
        StartScreen.LIBRARY -> R.string.nav_library
        StartScreen.VISUALS -> R.string.nav_visuals
        StartScreen.STUDIO -> R.string.nav_studio
    }
