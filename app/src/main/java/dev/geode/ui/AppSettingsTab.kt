package dev.geode.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import dev.geode.R
import dev.geode.ui.glass.GlassHorizontalTabs
import dev.geode.ui.glass.GlassPalette
import dev.geode.ui.glass.GlassShapes
import dev.geode.ui.glass.floatOnWater
import dev.geode.ui.glass.glassSurface

@Composable
internal fun AppSettingsTab(
    viewModel: PlayerViewModel,
    exportOpen: Boolean = false,
    onOpenExport: () -> Unit,
    onStartTutorial: () -> Unit,
) {
    val settingsViewModel: SettingsViewModel = geodeViewModel()
    var tab by rememberSaveable { mutableIntStateOf(0) }
    // Order matches the `when` below.
    val tabTitles =
        listOf(
            stringResource(R.string.settings_tab_look),
            stringResource(R.string.settings_tab_audio),
            stringResource(R.string.settings_tab_export),
            stringResource(R.string.settings_tab_folders),
            stringResource(R.string.settings_tab_behavior),
            stringResource(R.string.settings_tab_help),
            stringResource(R.string.settings_tab_about),
        )
    Column(Modifier.fillMaxSize()) {
        GlassHorizontalTabs(
            titles = tabTitles,
            selected = tab,
            onSelect = { tab = it },
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 10.dp),
        )
        when (tab) {
            0 -> LookSettingsTab(settingsViewModel)
            1 -> AudioSettingsTab(viewModel)
            2 -> ExportSettingsTab(exportOpen, onOpenExport)
            3 -> FolderSettingsTab()
            4 -> BehaviorSettingsTab(settingsViewModel)
            5 -> HelpSettingsTab(settingsViewModel, onStartTutorial)
            else -> AboutSettingsTab()
        }
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

/** A frosted glass tile: a title overline, an optional trailing header, and grouped content. */
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
            .glassSurface(shape = GlassShapes.tile)
            .floatOnWater(strength = 0.15f)
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            SettingsOverline(title, Modifier.weight(1f))
            header?.invoke(this)
        }
        content()
    }
}

/** A muted, letter-spaced section label used at the top of a [SettingsGroup] or a sub-section. */
@Composable
internal fun SettingsOverline(
    text: String,
    modifier: Modifier = Modifier,
) {
    Text(
        text,
        modifier,
        style = MaterialTheme.typography.labelMedium.copy(letterSpacing = 1.2.sp),
        color = GlassPalette.textSecondary,
    )
}
