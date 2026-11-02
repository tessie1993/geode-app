package dev.geode.ui

import androidx.annotation.StringRes
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import dev.geode.R
import dev.geode.data.BehaviourPrefs
import dev.geode.data.BehaviourStore
import dev.geode.data.GeodePrefsFiles
import dev.geode.data.NotificationButton
import dev.geode.data.PlayerArt
import dev.geode.data.QueueEndAction
import dev.geode.data.ShakeAction
import dev.geode.data.StartScreen
import dev.geode.data.SwipeRowAction
import dev.geode.data.TapSongAction
import dev.geode.data.VisualsBar
import dev.geode.data.VisualsOrientation
import dev.geode.data.VolumeKeysAction
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/**
 * Settings › Behaviour & gestures: the start screen, gestures, the queue, the screens and the
 * notification. The choices live in [BehaviourPrefs] (`geode-prefs`), whose defaults the owner
 * set. The gesture map on the visual (`BehaviourPrefs.gestures`) gets its own page with 3C.
 */
@Composable
internal fun BehaviorSettingsTab() {
    val behaviour = rememberBehaviourPrefs()
    val prefs = behaviour.prefs ?: return
    val update = behaviour.update
    SettingsTabColumn {
        item { SettingsGroup(stringResource(R.string.settings_beh_section_start)) { StartGroup(prefs, update) } }
        item { SettingsGroup(stringResource(R.string.settings_beh_section_gestures)) { GestureGroup(prefs, update) } }
        item { SettingsGroup(stringResource(R.string.settings_beh_section_queue)) { QueueGroup(prefs, update) } }
        item { SettingsGroup(stringResource(R.string.settings_beh_section_screens)) { ScreensGroup(prefs, update) } }
        item { SettingsGroup(stringResource(R.string.settings_beh_section_notification)) { NotificationGroup(prefs, update) } }
    }
}

/** [BehaviourPrefs] as a screen holds it: null until the first read (on IO) is back. */
internal class BehaviourPrefsHolder(
    val prefs: BehaviourPrefs?,
    val update: (BehaviourPrefs) -> Unit,
)

/**
 * Reads [BehaviourStore] once on IO, then keeps the screen's copy and writes each change through
 * (`apply()`, already async). Nothing else writes these keys while Settings is open.
 */
@Composable
internal fun rememberBehaviourPrefs(): BehaviourPrefsHolder {
    val ctx = LocalContext.current
    val store = remember { BehaviourStore(GeodePrefsFiles(ctx).general) }
    var prefs by remember { mutableStateOf<BehaviourPrefs?>(null) }
    LaunchedEffect(store) { prefs = withContext(Dispatchers.IO) { store.load() } }
    return BehaviourPrefsHolder(prefs) { next ->
        prefs = next
        store.save(next)
    }
}

@Composable
private fun StartGroup(
    prefs: BehaviourPrefs,
    update: (BehaviourPrefs) -> Unit,
) {
    SettingsChoiceRow(
        title = stringResource(R.string.settings_beh_start_screen),
        options = StartScreen.entries,
        selected = prefs.startScreen,
        label = { stringResource(startScreenLabel(it)) },
        onSelect = { update(prefs.copy(startScreen = it)) },
    )
    SettingsSwitchRow(
        title = stringResource(R.string.settings_beh_headphones),
        summary = stringResource(R.string.settings_beh_headphones_summary),
        checked = prefs.startOnHeadphones,
        onCheckedChange = { update(prefs.copy(startOnHeadphones = it)) },
    )
}

@Composable
private fun GestureGroup(
    prefs: BehaviourPrefs,
    update: (BehaviourPrefs) -> Unit,
) {
    SettingsChoiceRow(
        title = stringResource(R.string.settings_beh_shake),
        options = ShakeAction.entries,
        selected = prefs.shake,
        label = { stringResource(shakeLabel(it)) },
        onSelect = { update(prefs.copy(shake = it)) },
    )
    SettingsChoiceRow(
        title = stringResource(R.string.settings_beh_volume_keys),
        options = VolumeKeysAction.entries,
        selected = prefs.volumeKeysOnVisuals,
        label = {
            stringResource(
                when (it) {
                    VolumeKeysAction.VOLUME -> R.string.settings_beh_volume_keys_volume
                    VolumeKeysAction.STYLE -> R.string.settings_beh_volume_keys_style
                },
            )
        },
        onSelect = { update(prefs.copy(volumeKeysOnVisuals = it)) },
    )
}

@Composable
private fun QueueGroup(
    prefs: BehaviourPrefs,
    update: (BehaviourPrefs) -> Unit,
) {
    SettingsChoiceRow(
        title = stringResource(R.string.settings_beh_tap_song),
        options = TapSongAction.entries,
        selected = prefs.tapSong,
        label = {
            stringResource(
                when (it) {
                    TapSongAction.WHOLE_LIST -> R.string.settings_beh_tap_song_list
                    TapSongAction.ONLY_IT -> R.string.settings_beh_tap_song_only
                    TapSongAction.ADD_TO_QUEUE -> R.string.settings_beh_add_to_queue
                },
            )
        },
        onSelect = { update(prefs.copy(tapSong = it)) },
    )
    SettingsChoiceRow(
        title = stringResource(R.string.settings_beh_swipe_row),
        options = SwipeRowAction.entries,
        selected = prefs.swipeRow,
        label = {
            stringResource(
                when (it) {
                    SwipeRowAction.OFF -> R.string.settings_beh_nothing
                    SwipeRowAction.QUEUE_NEXT -> R.string.settings_beh_swipe_row_next
                    SwipeRowAction.ADD_TO_QUEUE -> R.string.settings_beh_add_to_queue
                },
            )
        },
        onSelect = { update(prefs.copy(swipeRow = it)) },
    )
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
        onSelect = { update(prefs.copy(queueEnd = it)) },
    )
    SettingsSwitchRow(
        title = stringResource(R.string.settings_beh_remember_shuffle),
        checked = prefs.rememberShuffleRepeat,
        onCheckedChange = { update(prefs.copy(rememberShuffleRepeat = it)) },
    )
}

@Composable
private fun ScreensGroup(
    prefs: BehaviourPrefs,
    update: (BehaviourPrefs) -> Unit,
) {
    SettingsChoiceRow(
        title = stringResource(R.string.settings_beh_orientation),
        options = VisualsOrientation.entries,
        selected = prefs.visualsOrientation,
        label = {
            stringResource(
                when (it) {
                    VisualsOrientation.AUTO_ROTATE -> R.string.settings_beh_orientation_auto
                    VisualsOrientation.PORTRAIT -> R.string.settings_beh_orientation_portrait
                    VisualsOrientation.LANDSCAPE -> R.string.settings_beh_orientation_landscape
                },
            )
        },
        onSelect = { update(prefs.copy(visualsOrientation = it)) },
    )
    SettingsChoiceRow(
        title = stringResource(R.string.settings_beh_player_art),
        options = PlayerArt.entries,
        selected = prefs.playerArt,
        label = {
            stringResource(
                when (it) {
                    PlayerArt.LIVE_VISUAL -> R.string.settings_beh_player_art_live
                    PlayerArt.ARTWORK -> R.string.settings_beh_player_art_artwork
                },
            )
        },
        onSelect = { update(prefs.copy(playerArt = it)) },
    )
    SettingsChoiceRow(
        title = stringResource(R.string.settings_beh_visuals_bar),
        options = VisualsBar.entries,
        selected = prefs.visualsBar,
        label = {
            stringResource(
                when (it) {
                    VisualsBar.ALWAYS -> R.string.settings_beh_visuals_bar_always
                    VisualsBar.AUTO_HIDE -> R.string.settings_beh_visuals_bar_auto
                    VisualsBar.NEVER -> R.string.settings_beh_visuals_bar_never
                },
            )
        },
        onSelect = { update(prefs.copy(visualsBar = it)) },
    )
    SettingsSwitchRow(
        title = stringResource(R.string.settings_beh_auto_fullscreen),
        checked = prefs.autoFullscreen,
        onCheckedChange = { update(prefs.copy(autoFullscreen = it)) },
    )
    SettingsSwitchRow(
        title = stringResource(R.string.settings_beh_keep_screen_on),
        checked = prefs.keepScreenOnVisuals,
        onCheckedChange = { update(prefs.copy(keepScreenOnVisuals = it)) },
    )
    SettingsSwitchRow(
        title = stringResource(R.string.settings_beh_remember_page),
        checked = prefs.rememberVisualsPage,
        onCheckedChange = { update(prefs.copy(rememberVisualsPage = it)) },
    )
    SettingsSwitchRow(
        title = stringResource(R.string.settings_beh_tips),
        checked = prefs.showTips,
        onCheckedChange = { update(prefs.copy(showTips = it)) },
    )
    SettingsSwitchRow(
        title = stringResource(R.string.settings_beh_confirm_exit),
        checked = prefs.confirmExit,
        onCheckedChange = { update(prefs.copy(confirmExit = it)) },
    )
}

/** The extra buttons the media notification carries beside play, previous and next. */
@Composable
private fun NotificationGroup(
    prefs: BehaviourPrefs,
    update: (BehaviourPrefs) -> Unit,
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
                update(prefs.copy(notificationButtons = if (on) prefs.notificationButtons + button else prefs.notificationButtons - button))
            },
        )
    }
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

@StringRes
private fun shakeLabel(action: ShakeAction): Int =
    when (action) {
        ShakeAction.OFF -> R.string.settings_beh_nothing
        ShakeAction.NEXT_TRACK -> R.string.settings_beh_shake_next_track
        ShakeAction.NEXT_STYLE -> R.string.settings_beh_shake_next_style
    }
