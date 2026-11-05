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
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowRight
import androidx.compose.material.icons.filled.Backup
import androidx.compose.material.icons.filled.GraphicEq
import androidx.compose.material.icons.filled.Palette
import androidx.compose.material.icons.filled.School
import androidx.compose.material.icons.filled.Speed
import androidx.compose.material.icons.filled.TouchApp
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import dev.geode.R
import dev.geode.model.TutorialChapter
import dev.geode.ui.pro.GoProSettingsRow
import dev.geode.ui.theme.GeodeRadius
import dev.geode.ui.theme.GeodeSpacing
import dev.geode.ui.theme.StoneIcon
import dev.geode.ui.theme.floatingGlass

/**
 * Settings' groups, in the order the list shows them: the order of how often people reach for
 * them in a music player (docs/research/settings-behaviour.md), Playback and Behaviour first, the
 * look and the app's own housekeeping last. Each group's page is Basics (always open) and
 * [sections] that drop open below it. Saved by name, so reordering never restores the wrong page.
 *
 * There is no Auto group: Auto (the auto-director, the style timer and its sources) is Visuals'
 * quick settings alone, where its effect is on screen.
 */
internal enum class SettingsGroupKey(
    @param:StringRes val titleRes: Int,
    @param:StringRes val summaryRes: Int,
    val sections: List<SettingsSectionKey> = emptyList(),
) {
    PLAYBACK(
        R.string.settings_group_playback,
        R.string.settings_group_playback_summary,
        listOf(
            SettingsSectionKey.SPEED_PITCH,
            SettingsSectionKey.VOLUME_LEVELLING,
            SettingsSectionKey.EQUALIZER,
            SettingsSectionKey.PLAYBACK_MORE,
        ),
    ),
    BEHAVIOUR(
        R.string.settings_group_behaviour,
        R.string.settings_group_behaviour_summary,
        listOf(
            SettingsSectionKey.GESTURES,
            SettingsSectionKey.NOTIFICATION,
            SettingsSectionKey.PICTURE_IN_PICTURE,
            SettingsSectionKey.INTENT,
        ),
    ),
    VISUALS(
        R.string.settings_group_visuals,
        R.string.settings_group_visuals_summary,
        listOf(SettingsSectionKey.TOUCH, SettingsSectionKey.CONNECTED_DISPLAY, SettingsSectionKey.WALLPAPER_DREAM),
    ),
    SOUND_INPUT(R.string.settings_group_sound_input, R.string.settings_group_sound_input_summary, listOf(SettingsSectionKey.ANALYSIS)),
    LOOK(
        R.string.settings_group_look,
        R.string.settings_group_look_summary,
        listOf(SettingsSectionKey.LOOK_TEXT, SettingsSectionKey.LOOK_LAYOUT),
    ),
    PERFORMANCE(R.string.settings_group_performance, R.string.settings_group_performance_summary, listOf(SettingsSectionKey.PERF_DETAILS)),
    LIBRARY(
        R.string.settings_group_library,
        R.string.settings_group_library_summary,
        listOf(SettingsSectionKey.LYRICS, SettingsSectionKey.OTHER_FOLDERS),
    ),
    EXPORT(R.string.settings_group_export, R.string.settings_group_export_summary),
    HELP(R.string.settings_group_help, R.string.settings_group_help_summary, listOf(SettingsSectionKey.HELP_TOPICS)),
    BACKUP_ABOUT(R.string.settings_group_about, R.string.settings_group_about_summary, listOf(SettingsSectionKey.ABOUT)),
    ;

    /** A Material Symbol from [StoneIcon] where the app has one, else the Material icon. */
    val icon: ImageVector
        get() =
            when (this) {
                PLAYBACK -> Icons.Filled.GraphicEq
                BEHAVIOUR -> Icons.Filled.TouchApp
                VISUALS -> StoneIcon.VISUALIZER.vector
                SOUND_INPUT -> StoneIcon.MICROPHONE.vector
                LOOK -> Icons.Filled.Palette
                PERFORMANCE -> Icons.Filled.Speed
                LIBRARY -> StoneIcon.LIBRARY.vector
                EXPORT -> StoneIcon.STUDIO.vector
                HELP -> Icons.Filled.School
                BACKUP_ABOUT -> Icons.Filled.Backup
            }

    companion object {
        fun byName(name: String?): SettingsGroupKey? = entries.firstOrNull { it.name == name }
    }
}

/** The sections below a group's Basics, each named for its card. */
internal enum class SettingsSectionKey(
    @param:StringRes val titleRes: Int,
) {
    SPEED_PITCH(R.string.settings_section_speed_pitch),
    VOLUME_LEVELLING(R.string.settings_section_volume_levelling),
    EQUALIZER(R.string.settings_section_equalizer),
    PLAYBACK_MORE(R.string.settings_section_playback_more),
    GESTURES(R.string.settings_section_gestures),
    NOTIFICATION(R.string.settings_section_notification),
    PICTURE_IN_PICTURE(R.string.behavior_group_pip),
    INTENT(R.string.behavior_group_intent),
    TOUCH(R.string.behavior_group_touch),
    CONNECTED_DISPLAY(R.string.behavior_group_display),
    WALLPAPER_DREAM(R.string.settings_section_wallpaper_dream),
    ANALYSIS(R.string.audio_group_analysis),
    LOOK_TEXT(R.string.look_group_text),
    LOOK_LAYOUT(R.string.look_group_layout),
    PERF_DETAILS(R.string.perf_details_title),
    LYRICS(R.string.settings_tab_lyrics),
    OTHER_FOLDERS(R.string.settings_section_other_folders),
    HELP_TOPICS(R.string.help_topics_group),
    ABOUT(R.string.settings_tab_about),
}

/**
 * Settings, in the pattern of the new Customize: a vertical list of groups (icon, name, one-line
 * summary). A group opens as its own page: ← back to the list, its title, and ‹ › to the group
 * before or after. On the page Basics are always open at the top; the sections below drop open in
 * place, and several can be open at once. Never wider than [SettingsMaxWidth], centred on a tablet.
 *
 * Back leaves a group before it leaves Settings; ‹ › step sideways without a back entry, so back
 * from any group returns to the list. The open group and the open sections are saved by name, so
 * rotation and process death come back to them.
 */
@Composable
internal fun AppSettingsTab(
    viewModel: PlayerViewModel,
    onOpenStudioRender: () -> Unit,
    onPlayTutorial: (List<TutorialChapter>) -> Unit,
    onResetTutorials: () -> Unit,
    requestedTab: Int? = null,
    onRequestedTabShown: () -> Unit = {},
) {
    var groupName by rememberSaveable { mutableStateOf<String?>(null) }
    // The open sections, as "GROUP/SECTION" names joined by ",", across every group: a group
    // stepped away from and back to opens as it was left.
    var openSections by rememberSaveable { mutableStateOf("") }
    val group = SettingsGroupKey.byName(groupName)
    val listState = rememberLazyListState()
    // Another screen asked for a group (the empty library's "Choose a music folder" → Library):
    // open it once, then hand the request back so a later visit opens where the user left it.
    LaunchedEffect(requestedGroup) {
        if (requestedGroup != null) {
            groupName = requestedGroup.name
            onRequestedGroupShown()
        }
    }
    BackHandler(enabled = group != null) { groupName = null }
    Box(Modifier.fillMaxSize(), contentAlignment = Alignment.TopCenter) {
        Column(Modifier.widthIn(max = SettingsMaxWidth).fillMaxSize()) {
            if (group == null) {
                LazyColumn(
                    Modifier.fillMaxSize(),
                    state = listState,
                    contentPadding = PaddingValues(horizontal = GeodeSpacing.L, vertical = GeodeSpacing.S),
                    verticalArrangement = Arrangement.spacedBy(GeodeSpacing.S),
                ) {
                    item { GoProSettingsRow() }
                    items(SettingsGroupKey.entries, key = { it.name }) { entry ->
                        SettingsGroupRow(entry) { groupName = entry.name }
                    }
                }
            } else {
                val open = openSections.split(',').filter { it.isNotEmpty() }.toSet()
                SettingsGroupHeader(
                    title = stringResource(group.titleRes),
                    index = group.ordinal,
                    count = SettingsGroupKey.entries.size,
                    onBack = { groupName = null },
                    onStep = { groupName = SettingsGroupKey.entries[it].name },
                    modifier = Modifier.padding(horizontal = GeodeSpacing.XS),
                )
                // Keyed by group, so a step to the next group starts at its top.
                key(group) {
                    Column(
                        Modifier
                            .fillMaxSize()
                            .verticalScroll(rememberScrollState())
                            .padding(horizontal = GeodeSpacing.L, vertical = GeodeSpacing.S),
                        verticalArrangement = Arrangement.spacedBy(GeodeSpacing.M),
                    ) {
                        SettingsGroupPage(
                            group = group,
                            viewModel = viewModel,
                            open = { "${group.name}/${it.name}" in open },
                            onToggle = { section ->
                                val name = "${group.name}/${section.name}"
                                openSections = (if (name in open) open - name else open + name).joinToString(",")
                            },
                            onOpenStudioRender = onOpenStudioRender,
                            onPlayTutorial = onPlayTutorial,
                            onResetTutorials = onResetTutorials,
                        )
                    }
                }
            }
        }
    }
}

/** One group in the list: its icon, name and one-line summary, and a chevron into it. */
@Composable
private fun SettingsGroupRow(
    group: SettingsGroupKey,
    onClick: () -> Unit,
) {
    val shape = RoundedCornerShape(GeodeRadius.Large)
    SettingsRow(
        title = stringResource(group.titleRes),
        summary = stringResource(group.summaryRes),
        icon = group.icon,
        modifier = Modifier.floatingGlass(shape = shape, elevation = 2.dp),
        contentPadding = PaddingValues(horizontal = GeodeSpacing.L, vertical = GeodeSpacing.M),
        onClick = onClick,
        trailing = {
            Icon(
                Icons.AutoMirrored.Filled.KeyboardArrowRight,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        },
    )
}

/** A group's page: Basics, then its sections, each open or closed by [open]. */
@Composable
private fun SettingsGroupPage(
    group: SettingsGroupKey,
    viewModel: PlayerViewModel,
    open: (SettingsSectionKey) -> Boolean,
    onToggle: (SettingsSectionKey) -> Unit,
    onOpenStudioRender: () -> Unit,
    onPlayTutorial: (List<TutorialChapter>) -> Unit,
    onResetTutorials: () -> Unit,
    requestedGroup: SettingsGroupKey? = null,
    onRequestedGroupShown: () -> Unit = {},
) {
    val settings: SettingsViewModel = geodeViewModel()
    // Behaviour and Visuals write the same prefs file; one copy for the page, read once on IO.
    val behaviour =
        if (group == SettingsGroupKey.BEHAVIOUR || group == SettingsGroupKey.VISUALS) rememberBehaviourPrefs() else null
    SettingsBasicsCard {
        when (group) {
            SettingsGroupKey.PLAYBACK -> PlaybackBasics(settings)
            SettingsGroupKey.BEHAVIOUR -> behaviour?.let { b -> b.prefs?.let { BehaviourBasics(it, b.update) } }
            SettingsGroupKey.VISUALS -> VisualsBasics()
            SettingsGroupKey.SOUND_INPUT -> SoundInputBasics(viewModel)
            SettingsGroupKey.LOOK -> LookBasics(settings)
            SettingsGroupKey.PERFORMANCE -> PerformanceBasics()
            SettingsGroupKey.LIBRARY -> MusicFoldersBasics()
            SettingsGroupKey.EXPORT -> ExportLinkBasics(onOpenStudioRender)
            SettingsGroupKey.HELP -> TutorialSettingsTab(settings, onPlay = onPlayTutorial, onReset = onResetTutorials)
            SettingsGroupKey.BACKUP_ABOUT -> BackupSection()
        }
    }
    group.sections.forEach { section ->
        SettingsSectionCard(
            title = stringResource(section.titleRes),
            expanded = open(section),
            onToggle = { onToggle(section) },
        ) {
            SettingsSectionBody(section, viewModel, settings, behaviour)
        }
    }
}

@Composable
private fun SettingsSectionBody(
    section: SettingsSectionKey,
    viewModel: PlayerViewModel,
    settings: SettingsViewModel,
    behaviour: BehaviourPrefsHolder?,
) {
    when (section) {
        SettingsSectionKey.SPEED_PITCH -> SpeedPitchSection(settings)
        SettingsSectionKey.VOLUME_LEVELLING -> VolumeLevellingSection(settings)
        SettingsSectionKey.EQUALIZER -> EqualizerSettings(settings)
        SettingsSectionKey.PLAYBACK_MORE -> PlaybackMoreSection(settings)
        SettingsSectionKey.GESTURES -> behaviour?.let { b -> b.prefs?.let { GesturesSection(it, b.update) } }
        SettingsSectionKey.NOTIFICATION -> behaviour?.let { b -> b.prefs?.let { NotificationButtonsSection(it, b.update) } }
        SettingsSectionKey.PICTURE_IN_PICTURE -> PictureInPictureSection(settings)
        SettingsSectionKey.INTENT -> IntentSection(settings)
        SettingsSectionKey.TOUCH -> TouchSection(settings)
        SettingsSectionKey.CONNECTED_DISPLAY -> ConnectedDisplaySection(settings)
        SettingsSectionKey.WALLPAPER_DREAM -> behaviour?.let { b -> b.prefs?.let { WallpaperDreamSection(it, b.update) } }
        SettingsSectionKey.ANALYSIS -> SoundAnalysisSection(settings)
        SettingsSectionKey.LOOK_TEXT -> LookTextSection(settings)
        SettingsSectionKey.LOOK_LAYOUT -> LookLayoutSection(settings)
        SettingsSectionKey.PERF_DETAILS -> PerformanceDetailsSection()
        SettingsSectionKey.LYRICS -> LyricsSection(viewModel)
        SettingsSectionKey.OTHER_FOLDERS -> OtherFoldersSection()
        SettingsSectionKey.HELP_TOPICS -> HelpTopicsSection()
        SettingsSectionKey.ABOUT -> AboutSection()
    }
}

/**
 * Export, loop render and their defaults belong to Studio › Render; this group only points there,
 * so someone who looks for them in Settings (where they used to be) still finds the way.
 */
@Composable
private fun ExportLinkBasics(onOpenStudioRender: () -> Unit) {
    Text(
        stringResource(R.string.settings_export_moved),
        style = MaterialTheme.typography.bodySmall,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
    )
    CrystalButton(onClick = onOpenStudioRender) { Text(stringResource(R.string.settings_export_open_studio)) }
}

/** A lazy list of [SettingsGroup] cards; Studio › Render's options still use it. */
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
