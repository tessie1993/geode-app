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
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

internal val SETTINGS_TAB_TITLES = listOf("Look", "Audio", "Export", "Folders", "Behavior", "Help", "About")

@Composable
internal fun AppSettingsTab(
    viewModel: PlayerViewModel,
    exportOpen: Boolean = false,
    onOpenExport: () -> Unit,
    onStartTutorial: () -> Unit,
) {
    val settingsViewModel: SettingsViewModel = geodeViewModel()
    var tab by rememberSaveable { mutableIntStateOf(0) }
    Column(Modifier.fillMaxSize()) {
        CrystalTabs(titles = SETTINGS_TAB_TITLES, selected = tab, onSelect = { tab = it })
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
            CrystalOverline(title, Modifier.weight(1f))
            header?.invoke(this)
        }
        content()
    }
}
