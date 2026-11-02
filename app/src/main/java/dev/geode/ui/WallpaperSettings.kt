package dev.geode.ui

import android.app.WallpaperManager
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.provider.Settings
import androidx.compose.runtime.Composable
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import dev.geode.R
import dev.geode.pro.ProFeature
import dev.geode.ui.wallpaper.WallpaperOptionsActivity
import dev.geode.wallpaper.VisualizerWallpaperService

/**
 * Settings › Wallpaper & screensaver: the visual as the live wallpaper and as the screen saver
 * (both Pro), the wallpaper's own options, and the screen saver's brightness, clock and
 * now-playing line ([dev.geode.data.BehaviourPrefs], read by the dream).
 */
@Composable
internal fun WallpaperSettingsPage() {
    val ctx = LocalContext.current
    val pro = rememberProAccess()
    val behaviour = rememberBehaviourPrefs()
    SettingsTabColumn {
        item {
            SettingsGroup(stringResource(R.string.behavior_group_wallpaper)) {
                SettingsRow(
                    title = stringResource(R.string.behavior_wallpaper_button),
                    summary = stringResource(R.string.settings_wallpaper_set_summary),
                    pro = true,
                    onClick = { pro.runOrPaywall(ProFeature.LIVE_WALLPAPER) { openLiveWallpaperPicker(ctx) } },
                )
                SettingsRow(
                    title = stringResource(R.string.wallpaper_options_title),
                    summary = stringResource(R.string.settings_wallpaper_options_summary),
                    onClick = { ctx.startActivity(Intent(ctx, WallpaperOptionsActivity::class.java)) },
                )
            }
        }
        item {
            SettingsGroup(stringResource(R.string.settings_screensaver)) {
                SettingsRow(
                    title = stringResource(R.string.settings_screensaver_choose),
                    summary = stringResource(R.string.settings_screensaver_choose_summary),
                    pro = true,
                    onClick = { pro.runOrPaywall(ProFeature.SCREENSAVER) { openScreenSaverSettings(ctx) } },
                )
                val prefs = behaviour.prefs
                if (prefs != null) {
                    SettingsSwitchRow(
                        title = stringResource(R.string.settings_screensaver_bright),
                        summary = stringResource(R.string.settings_screensaver_bright_summary),
                        checked = prefs.dreamScreenBright,
                        onCheckedChange = { behaviour.update(prefs.copy(dreamScreenBright = it)) },
                    )
                    SettingsSwitchRow(
                        title = stringResource(R.string.settings_screensaver_clock),
                        checked = prefs.dreamShowClock,
                        onCheckedChange = { behaviour.update(prefs.copy(dreamShowClock = it)) },
                    )
                    SettingsSwitchRow(
                        title = stringResource(R.string.settings_screensaver_now_playing),
                        summary = stringResource(R.string.settings_screensaver_now_playing_summary),
                        checked = prefs.dreamShowNowPlaying,
                        onCheckedChange = { behaviour.update(prefs.copy(dreamShowNowPlaying = it)) },
                    )
                }
            }
        }
    }
}

/** Android's live wallpaper preview on this app's service, else the general wallpaper picker. */
private fun openLiveWallpaperPicker(ctx: Context) {
    val direct =
        Intent(WallpaperManager.ACTION_CHANGE_LIVE_WALLPAPER).putExtra(
            WallpaperManager.EXTRA_LIVE_WALLPAPER_COMPONENT,
            ComponentName(ctx, VisualizerWallpaperService::class.java),
        )
    if (runCatching { ctx.startActivity(direct) }.isFailure) {
        runCatching { ctx.startActivity(Intent(Intent.ACTION_SET_WALLPAPER)) }
    }
}

/** Android's screen saver page, where the user picks MuzViz; the general Display page as a fallback. */
private fun openScreenSaverSettings(ctx: Context) {
    if (runCatching { ctx.startActivity(Intent(Settings.ACTION_DREAM_SETTINGS)) }.isFailure) {
        runCatching { ctx.startActivity(Intent(Settings.ACTION_DISPLAY_SETTINGS)) }
    }
}
