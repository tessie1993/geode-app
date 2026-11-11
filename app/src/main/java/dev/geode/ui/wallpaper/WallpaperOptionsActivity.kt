package dev.geode.ui.wallpaper

import android.Manifest
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.SystemBarStyle
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import dev.geode.R
import dev.geode.geodeContainer
import dev.geode.ui.CrystalMaterialTheme
import dev.geode.ui.CrystalSegmented
import dev.geode.ui.LabeledSwitch
import dev.geode.ui.SettingsGroup
import dev.geode.ui.rememberPermissionRequest
import dev.geode.ui.theme.StoneIcon
import dev.geode.ui.theme.StoneIconArt
import dev.geode.wallpaper.WallpaperLook
import dev.geode.wallpaper.WallpaperPrefsStore
import dev.geode.wallpaper.WallpaperSettings

/**
 * The live wallpaper's options, opened by the Settings button of the system wallpaper picker
 * (`android:settingsActivity` in `res/xml/wallpaper.xml`).
 *
 * Its own small activity rather than a route into MainActivity: the picker starts it by
 * component with no extras, it must open over the picker and return there on back, and it
 * needs none of the app's session (no player, no library). A plain ComponentActivity with no
 * Hilt graph keeps that cold start to a theme and five switches.
 *
 * The wallpaper engine re-reads these options each time it becomes visible, which is exactly
 * when this page closes and the picker's preview (or the home screen) shows again.
 */
class WallpaperOptionsActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        // The page is black in every system mode: SystemBarStyle.auto would draw dark bar icons on it
        // under a light system theme (MainActivity does the same).
        enableEdgeToEdge(
            statusBarStyle = SystemBarStyle.dark(android.graphics.Color.TRANSPARENT),
            navigationBarStyle = SystemBarStyle.dark(android.graphics.Color.TRANSPARENT),
        )
        super.onCreate(savedInstanceState)
        val general = geodeContainer.general.current()
        val store = WallpaperPrefsStore(geodeContainer.prefsFiles.wallpaper)
        setContent {
            CrystalMaterialTheme(general) {
                WallpaperOptionsScreen(store, onClose = ::finish)
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun WallpaperOptionsScreen(
    store: WallpaperPrefsStore,
    onClose: () -> Unit,
) {
    var settings by remember { mutableStateOf(store.load()) }

    fun update(next: WallpaperSettings) {
        settings = next
        store.save(next)
    }
    Scaffold(
        containerColor = Color.Black,
        topBar = {
            TopAppBar(
                title = { Text(stringResource(R.string.wallpaper_options_title)) },
                navigationIcon = {
                    IconButton(onClick = onClose) {
                        StoneIconArt(StoneIcon.CLOSE, stringResource(R.string.wallpaper_options_close))
                    }
                },
                colors = TopAppBarDefaults.topAppBarColors(containerColor = Color.Black),
            )
        },
    ) { padding ->
        LazyColumn(
            Modifier.fillMaxSize(),
            contentPadding =
                PaddingValues(
                    start = 16.dp,
                    end = 16.dp,
                    top = padding.calculateTopPadding() + 8.dp,
                    bottom = padding.calculateBottomPadding() + 16.dp,
                ),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            item {
                SettingsGroup(stringResource(R.string.wallpaper_options_group_sound)) {
                    OtherAppsSwitch(settings) { update(it) }
                }
            }
            item {
                SettingsGroup(stringResource(R.string.wallpaper_options_group_battery)) {
                    LabeledSwitch(
                        stringResource(R.string.wallpaper_options_rest),
                        checked = settings.restWhenSilent,
                        onCheckedChange = { update(settings.copy(restWhenSilent = it)) },
                    )
                    Hint(stringResource(R.string.wallpaper_options_rest_hint))
                    LabeledSwitch(
                        stringResource(R.string.wallpaper_options_saver),
                        checked = settings.pauseInBatterySaver,
                        onCheckedChange = { update(settings.copy(pauseInBatterySaver = it)) },
                    )
                    Hint(stringResource(R.string.wallpaper_options_saver_hint))
                    Hint(stringResource(R.string.wallpaper_options_cover_hint))
                }
            }
            item {
                SettingsGroup(stringResource(R.string.wallpaper_options_group_look)) {
                    LookChoice(stringResource(R.string.wallpaper_options_home), settings.homeLook) {
                        update(settings.copy(homeLook = it))
                    }
                    LookChoice(stringResource(R.string.wallpaper_options_lock), settings.lockLook) {
                        update(settings.copy(lockLook = it))
                    }
                    Hint(stringResource(R.string.wallpaper_options_edge_hint))
                }
            }
        }
    }
}

/**
 * "React to any app": the switch shows the saved choice, and turning it on asks for the
 * permission in place. While it is on without the permission, one button asks again (or, once
 * Android has stopped asking, opens the app's system settings), so a refusal is never a switch
 * that silently flips back.
 */
@Composable
private fun OtherAppsSwitch(
    settings: WallpaperSettings,
    onChange: (WallpaperSettings) -> Unit,
) {
    val permission = rememberPermissionRequest(Manifest.permission.RECORD_AUDIO)
    Column {
        LabeledSwitch(
            stringResource(R.string.wallpaper_options_other_apps),
            checked = settings.hearOtherApps,
            onCheckedChange = { on ->
                onChange(settings.copy(hearOtherApps = on))
                if (on && !permission.granted) permission.request()
            },
        )
        Hint(stringResource(R.string.wallpaper_options_other_apps_hint))
        if (settings.hearOtherApps && !permission.granted) {
            TextButton(onClick = permission::request) {
                Text(stringResource(R.string.wallpaper_options_open_settings))
            }
        }
    }
}

@Composable
private fun LookChoice(
    label: String,
    look: WallpaperLook,
    onSelect: (WallpaperLook) -> Unit,
) {
    Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
        Text(label, style = MaterialTheme.typography.bodyMedium)
        CrystalSegmented(
            options =
                listOf(
                    stringResource(R.string.wallpaper_options_look_full),
                    stringResource(R.string.wallpaper_options_look_edge),
                ),
            selected = look.ordinal,
            onSelect = { onSelect(WallpaperLook.entries[it]) },
        )
    }
}

@Composable
private fun Hint(text: String) {
    Text(
        text,
        style = MaterialTheme.typography.bodySmall,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
    )
}
