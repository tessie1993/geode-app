package dev.geode.ui

import androidx.activity.compose.BackHandler
import androidx.annotation.StringRes
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.ArrowBack
import androidx.compose.material.icons.automirrored.rounded.HelpOutline
import androidx.compose.material.icons.automirrored.rounded.KeyboardArrowRight
import androidx.compose.material.icons.automirrored.rounded.Notes
import androidx.compose.material.icons.rounded.Backup
import androidx.compose.material.icons.rounded.Folder
import androidx.compose.material.icons.rounded.GraphicEq
import androidx.compose.material.icons.rounded.Info
import androidx.compose.material.icons.rounded.Movie
import androidx.compose.material.icons.rounded.Palette
import androidx.compose.material.icons.rounded.School
import androidx.compose.material.icons.rounded.Speed
import androidx.compose.material.icons.rounded.Tune
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import dev.geode.R
import dev.geode.model.TutorialChapter
import dev.geode.ui.pro.GoProSettingsRow

/**
 * Settings' sections, in the order and groups the index shows them.
 *
 * Settings used to be one scrolling tab strip of eleven names, which hid half of them off the
 * right edge on a phone: people did not find Backup or Tutorials because they never scrolled the
 * strip. A grouped list with one line under each name shows every section at once and says what
 * is inside before the tap, the way the system Settings app does.
 */
private enum class SettingsPage(
    @StringRes val title: Int,
    @StringRes val summary: Int,
    @StringRes val group: Int,
    val icon: ImageVector,
) {
    LOOK(R.string.settings_tab_look, R.string.settings_look_summary, R.string.settings_group_app, Icons.Rounded.Palette),
    BEHAVIOR(R.string.settings_tab_behavior, R.string.settings_behavior_summary, R.string.settings_group_app, Icons.Rounded.Tune),
    PERFORMANCE(
        R.string.settings_tab_performance,
        R.string.settings_performance_summary,
        R.string.settings_group_app,
        Icons.Rounded.Speed,
    ),
    AUDIO(R.string.settings_tab_audio, R.string.settings_audio_summary, R.string.settings_group_music, Icons.Rounded.GraphicEq),
    LYRICS(
        R.string.settings_tab_lyrics,
        R.string.settings_lyrics_summary,
        R.string.settings_group_music,
        Icons.AutoMirrored.Rounded.Notes,
    ),
    FOLDERS(R.string.settings_tab_folders, R.string.settings_folders_summary, R.string.settings_group_music, Icons.Rounded.Folder),

    /** Not a page: export lives in Studio › Render, so this row goes straight there. */
    EXPORT(R.string.settings_tab_export, R.string.settings_export_summary, R.string.settings_group_video, Icons.Rounded.Movie),
    HELP(
        R.string.settings_tab_help,
        R.string.settings_help_summary,
        R.string.settings_group_help,
        Icons.AutoMirrored.Rounded.HelpOutline,
    ),
    TUTORIALS(R.string.settings_tab_tutorials, R.string.settings_tutorials_summary, R.string.settings_group_help, Icons.Rounded.School),
    BACKUP(R.string.backup_tab, R.string.settings_backup_summary, R.string.settings_group_help, Icons.Rounded.Backup),
    ABOUT(R.string.settings_tab_about, R.string.settings_about_summary, R.string.settings_group_help, Icons.Rounded.Info),
}

@Composable
internal fun AppSettingsTab(
    viewModel: PlayerViewModel,
    onOpenStudioRender: () -> Unit,
    onPlayTutorial: (List<TutorialChapter>) -> Unit,
    onResetTutorials: () -> Unit,
) {
    val settingsViewModel: SettingsViewModel = geodeViewModel()
    // Saved by name, like the app's other screen state, so a page survives rotation and process death.
    var openName by rememberSaveable { mutableStateOf<String?>(null) }
    val open = openName?.let { name -> SettingsPage.entries.firstOrNull { it.name == name } }
    // Back closes the page before it leaves Settings, as Customize › Advanced does with its cards.
    BackHandler(enabled = open != null) { openName = null }
    Column(Modifier.fillMaxSize()) {
        if (open == null) {
            SettingsHeader(title = stringResource(R.string.nav_settings), onBack = null)
            SettingsIndex(
                onOpen = { page ->
                    if (page == SettingsPage.EXPORT) onOpenStudioRender() else openName = page.name
                },
            )
        } else {
            SettingsHeader(title = stringResource(open.title), onBack = { openName = null })
            when (open) {
                SettingsPage.LOOK -> LookSettingsTab(settingsViewModel)
                SettingsPage.BEHAVIOR -> BehaviorSettingsTab(settingsViewModel)
                SettingsPage.PERFORMANCE -> PerformanceSettingsTab()
                SettingsPage.AUDIO -> AudioSettingsTab(viewModel)
                SettingsPage.LYRICS -> LyricsSettingsTab(viewModel)
                SettingsPage.FOLDERS -> FolderSettingsTab()
                SettingsPage.EXPORT -> Unit
                SettingsPage.HELP -> HelpSettingsTab()
                SettingsPage.TUTORIALS ->
                    TutorialSettingsTab(settingsViewModel, onPlay = onPlayTutorial, onReset = onResetTutorials)
                SettingsPage.BACKUP -> BackupSettingsTab()
                SettingsPage.ABOUT -> AboutSettingsTab()
            }
        }
    }
}

/** The screen title; inside a page it names the page and carries the way back to the index. */
@Composable
private fun SettingsHeader(
    title: String,
    onBack: (() -> Unit)?,
) {
    if (onBack == null) {
        Column(Modifier.padding(start = 16.dp, end = 16.dp, top = 16.dp)) {
            CrystalOverline(stringResource(R.string.app_name))
            GlowTitle(title)
        }
    } else {
        Row(
            Modifier.padding(start = 4.dp, end = 16.dp, top = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            IconButton(onClick = onBack) {
                Icon(Icons.AutoMirrored.Rounded.ArrowBack, stringResource(R.string.action_back))
            }
            GlowTitle(title)
        }
    }
}

@Composable
private fun SettingsIndex(onOpen: (SettingsPage) -> Unit) {
    val groups = SettingsPage.entries.groupBy { it.group }
    LazyColumn(
        Modifier.fillMaxSize(),
        contentPadding = PaddingValues(bottom = 14.dp),
    ) {
        item { GoProSettingsRow() }
        groups.forEach { (group, pages) ->
            item(key = group) {
                Column(Modifier.padding(horizontal = 16.dp, vertical = 6.dp)) {
                    CrystalOverline(
                        stringResource(group),
                        Modifier.padding(start = 4.dp, top = 10.dp, bottom = 8.dp).semantics { heading() },
                    )
                    Column(
                        Modifier
                            .fillMaxWidth()
                            .crystalPanel(
                                0.30f,
                                MaterialTheme.colorScheme.surfaceVariant,
                                MaterialTheme.colorScheme.primary,
                                corner = 18.dp,
                                glowStrength = 0.45f,
                            ),
                    ) {
                        pages.forEach { page -> SettingsIndexRow(page, onClick = { onOpen(page) }) }
                    }
                }
            }
        }
    }
}

@Composable
private fun SettingsIndexRow(
    page: SettingsPage,
    onClick: () -> Unit,
) {
    val cs = MaterialTheme.colorScheme
    Row(
        Modifier
            .fillMaxWidth()
            .heightIn(min = 64.dp)
            .clip(MaterialTheme.shapes.large)
            .clickable(role = Role.Button, onClick = onClick)
            .padding(horizontal = 14.dp, vertical = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(14.dp),
    ) {
        Box(
            Modifier.size(38.dp).background(cs.primary.copy(alpha = 0.14f), CircleShape),
            contentAlignment = Alignment.Center,
        ) {
            Icon(page.icon, contentDescription = null, tint = cs.primary, modifier = Modifier.size(20.dp))
        }
        Column(Modifier.weight(1f)) {
            Text(stringResource(page.title), style = MaterialTheme.typography.titleMedium, color = cs.onSurface)
            Text(
                stringResource(page.summary),
                style = MaterialTheme.typography.bodySmall,
                color = cs.onSurfaceVariant,
            )
        }
        Icon(
            Icons.AutoMirrored.Rounded.KeyboardArrowRight,
            contentDescription = null,
            tint = cs.onSurfaceVariant,
        )
    }
}

@Composable
internal fun SettingsTabColumn(content: LazyListScope.() -> Unit) {
    LazyColumn(
        Modifier.fillMaxSize(),
        contentPadding = PaddingValues(horizontal = 16.dp, vertical = 14.dp),
        verticalArrangement = Arrangement.spacedBy(14.dp),
        content = content,
    )
}

@Composable
internal fun SettingsGroup(
    title: String,
    modifier: Modifier = Modifier,
    header: (@Composable RowScope.() -> Unit)? = null,
    content: @Composable ColumnScope.() -> Unit,
) {
    Column(
        modifier
            .fillMaxWidth()
            .crystalPanel(
                0.30f,
                MaterialTheme.colorScheme.surfaceVariant,
                MaterialTheme.colorScheme.primary,
                corner = 18.dp,
                glowStrength = 0.45f,
            ).padding(14.dp),
        verticalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            CrystalOverline(title, Modifier.weight(1f).semantics { heading() })
            header?.invoke(this)
        }
        content()
    }
}
