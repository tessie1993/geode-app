// The file is named for AppSettingsTab, its entry point; SettingsPage is its one class.
@file:Suppress("MatchingDeclarationName")

package dev.geode.ui

import androidx.activity.compose.BackHandler
import androidx.annotation.StringRes
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.ArrowBack
import androidx.compose.material.icons.rounded.Backup
import androidx.compose.material.icons.rounded.GraphicEq
import androidx.compose.material.icons.rounded.School
import androidx.compose.material.icons.rounded.Smartphone
import androidx.compose.material.icons.rounded.TouchApp
import androidx.compose.material.icons.rounded.Wallpaper
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LargeTopAppBar
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.material3.TopAppBarScrollBehavior
import androidx.compose.material3.rememberTopAppBarState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import dev.geode.R
import dev.geode.model.TutorialChapter
import dev.geode.ui.pro.GoProSettingsRow
import dev.geode.ui.theme.GeodeSpacing
import dev.geode.ui.theme.StoneIcon

/**
 * Settings' groups, in the order the list shows them. Each opens its own page; there is no Pro
 * group: a Pro option sits in the group it belongs to, bolder and with a crown.
 */
internal enum class SettingsPage(
    @param:StringRes val titleRes: Int,
    @param:StringRes val summaryRes: Int,
) {
    PLAYBACK(R.string.settings_group_playback, R.string.settings_group_playback_summary),
    LIBRARY(R.string.settings_group_library, R.string.settings_group_library_summary),
    DISPLAY(R.string.settings_group_display, R.string.settings_group_display_summary),
    EXPORT(R.string.settings_group_export, R.string.settings_group_export_summary),
    WALLPAPER(R.string.settings_group_wallpaper, R.string.settings_group_wallpaper_summary),
    BEHAVIOUR(R.string.settings_group_behaviour, R.string.settings_group_behaviour_summary),
    HELP(R.string.settings_group_help, R.string.settings_group_help_summary),
    ABOUT(R.string.settings_group_about, R.string.settings_group_about_summary),
    ;

    /** A Material Symbol from [StoneIcon] where the app has one, else the Rounded Material icon. */
    val icon: ImageVector
        get() =
            when (this) {
                PLAYBACK -> Icons.Rounded.GraphicEq
                LIBRARY -> StoneIcon.LIBRARY.vector
                DISPLAY -> Icons.Rounded.Smartphone
                EXPORT -> StoneIcon.STUDIO.vector
                WALLPAPER -> Icons.Rounded.Wallpaper
                BEHAVIOUR -> Icons.Rounded.TouchApp
                HELP -> Icons.Rounded.School
                ABOUT -> Icons.Rounded.Backup
            }

    companion object {
        fun byName(name: String?): SettingsPage? = entries.firstOrNull { it.name == name }
    }
}

/**
 * Settings: the standard Android list of groups, each drilling into its page with a back arrow
 * and a title. System Back leaves a page before it leaves Settings. The page shown is saved by
 * name, so rotation and process death come back to it, and the list keeps its scroll while a
 * page is open. Never wider than [SettingsMaxWidth], centred on a tablet.
 *
 * The header is the M3 large top app bar ("Settings", or the page's title with its back arrow),
 * collapsing as the content scrolls (exit until collapsed). Where the window is short (a phone in
 * landscape) it is the small top app bar instead, so the header never takes the screen.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
internal fun AppSettingsTab(
    viewModel: PlayerViewModel,
    onOpenStudioRender: () -> Unit,
    onPlayTutorial: (List<TutorialChapter>) -> Unit,
    onResetTutorials: () -> Unit,
) {
    val settingsViewModel: SettingsViewModel = geodeViewModel()
    var pageName by rememberSaveable { mutableStateOf<String?>(null) }
    val page = SettingsPage.byName(pageName)
    val listState = rememberLazyListState()
    BackHandler(enabled = page != null) { pageName = null }
    Box(Modifier.fillMaxSize(), contentAlignment = Alignment.TopCenter) {
        // One bar state per screen: a page opens with its header expanded, whatever the list did.
        val barState = key(pageName) { rememberTopAppBarState() }
        val scrollBehavior = TopAppBarDefaults.exitUntilCollapsedScrollBehavior(barState)
        Column(
            Modifier
                .widthIn(max = SettingsMaxWidth)
                .fillMaxSize()
                .nestedScroll(scrollBehavior.nestedScrollConnection),
        ) {
            SettingsTopBar(
                title = stringResource(page?.titleRes ?: R.string.nav_settings),
                onBack = if (page != null) ({ pageName = null }) else null,
                scrollBehavior = scrollBehavior,
            )
            if (page == null) {
                LazyColumn(
                    Modifier.fillMaxSize(),
                    state = listState,
                    contentPadding = PaddingValues(vertical = GeodeSpacing.S),
                ) {
                    item { GoProSettingsRow() }
                    items(SettingsPage.entries, key = { it.name }) { group ->
                        SettingsRow(
                            title = stringResource(group.titleRes),
                            summary = stringResource(group.summaryRes),
                            icon = group.icon,
                            contentPadding = PaddingValues(horizontal = GeodeSpacing.L, vertical = GeodeSpacing.M),
                            onClick = { pageName = group.name },
                        )
                    }
                }
            } else {
                when (page) {
                    SettingsPage.PLAYBACK -> AudioSettingsTab(viewModel)
                    SettingsPage.LIBRARY -> FolderSettingsTab()
                    SettingsPage.DISPLAY -> DisplaySettingsPage(settingsViewModel)
                    SettingsPage.EXPORT -> ExportSettingsPage(onOpenStudioRender)
                    SettingsPage.WALLPAPER -> WallpaperSettingsPage()
                    SettingsPage.BEHAVIOUR -> BehaviorSettingsTab()
                    SettingsPage.HELP ->
                        TutorialSettingsTab(settingsViewModel, onPlay = onPlayTutorial, onReset = onResetTutorials)
                    SettingsPage.ABOUT -> BackupAboutPage()
                }
            }
        }
    }
}

/**
 * Settings' header: large and collapsing, or small where the window is shorter than
 * [SHORT_WINDOW_DP]. Transparent on the black canvas; the shell has already consumed the system
 * bars' insets, so the bar adds none of its own.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun SettingsTopBar(
    title: String,
    onBack: (() -> Unit)?,
    scrollBehavior: TopAppBarScrollBehavior,
) {
    val shortWindow = LocalConfiguration.current.screenHeightDp < SHORT_WINDOW_DP
    val colors =
        TopAppBarDefaults.topAppBarColors(
            containerColor = Color.Transparent,
            scrolledContainerColor = Color.Transparent,
        )
    val navigation: @Composable () -> Unit = {
        if (onBack != null) {
            IconButton(onClick = onBack) {
                Icon(Icons.AutoMirrored.Rounded.ArrowBack, contentDescription = stringResource(R.string.action_back))
            }
        }
    }
    val heading: @Composable () -> Unit = { Text(title, modifier = Modifier.semantics { heading() }) }
    if (shortWindow) {
        TopAppBar(
            title = heading,
            navigationIcon = navigation,
            colors = colors,
            windowInsets = WindowInsets(0, 0, 0, 0),
            scrollBehavior = scrollBehavior,
        )
    } else {
        LargeTopAppBar(
            title = heading,
            navigationIcon = navigation,
            colors = colors,
            windowInsets = WindowInsets(0, 0, 0, 0),
            scrollBehavior = scrollBehavior,
        )
    }
}

/** Below this window height (dp) the header is the small top app bar: a phone held sideways. */
private const val SHORT_WINDOW_DP = 480

@Composable
internal fun SettingsTabColumn(content: LazyListScope.() -> Unit) {
    LazyColumn(
        Modifier.fillMaxSize(),
        contentPadding = PaddingValues(horizontal = GeodeSpacing.L, vertical = GeodeSpacing.S),
        // The space between sections is what groups them, now that there is no panel.
        verticalArrangement = Arrangement.spacedBy(GeodeSpacing.XL),
        content = content,
    )
}

/**
 * A titled run of related controls on a page: a section of a settings screen. Plain rows on the
 * black canvas under a Label Large title, as Android's own Settings draws them: no panel, no
 * glow, no overline. Studio › Render and the wallpaper options use it too.
 */
@Composable
internal fun SettingsGroup(
    title: String,
    modifier: Modifier = Modifier,
    header: (@Composable RowScope.() -> Unit)? = null,
    content: @Composable ColumnScope.() -> Unit,
) {
    Column(
        modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(GeodeSpacing.S),
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            SettingsSectionTitle(title, Modifier.weight(1f))
            header?.invoke(this)
        }
        content()
    }
}
