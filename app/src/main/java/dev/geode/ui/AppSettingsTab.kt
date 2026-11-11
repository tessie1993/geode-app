package dev.geode.ui

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
import androidx.compose.material.icons.filled.Accessibility
import androidx.compose.material.icons.filled.Backup
import androidx.compose.material.icons.filled.Equalizer
import androidx.compose.material.icons.filled.Headphones
import androidx.compose.material.icons.filled.Palette
import androidx.compose.material.icons.filled.Speed
import androidx.compose.material.icons.filled.TouchApp
import androidx.compose.material.icons.filled.Wallpaper
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
import dev.geode.model.TourChapter
import dev.geode.ui.pro.GoProSettingsRow
import dev.geode.ui.theme.GeodeRadius
import dev.geode.ui.theme.GeodeSpacing
import dev.geode.ui.theme.StoneIcon
import dev.geode.ui.theme.edgeFade
import dev.geode.ui.theme.floatingGlass

/**
 * Settings' fourteen tabs, in the order the list shows them (owner, 2026-11-11, after a survey of
 * Poweramp, Symfonium and VJ apps' settings): how often people reach for each in
 * a music player and visualizer, playback and sound first, the look and the app's own housekeeping
 * last. Each tab's page is [basics] (always open) and [sections] that drop open below it.
 *
 * Saved by name, so reordering never restores the wrong page; [fromName] also takes the names of
 * the ten groups before the regroup, so a page saved then still opens on the tab its rows went to.
 * Every row is placed exactly once across all tabs ([SettingsRowKey], pinned by `SettingsMapTest`).
 */
enum class SettingsGroupKey(
    @param:StringRes val titleRes: Int,
    @param:StringRes val summaryRes: Int,
    val basics: List<SettingsRowKey>,
    val sections: List<SettingsSectionKey> = emptyList(),
) {
    PLAYBACK(
        R.string.settings_group_playback,
        R.string.settings_group_playback_summary,
        listOf(
            SettingsRowKey.CROSSFADE,
            SettingsRowKey.FADE,
            SettingsRowKey.AUTO_RESUME,
            SettingsRowKey.SLEEP_TIMER,
            SettingsRowKey.QUEUE_END,
            SettingsRowKey.REMEMBER_SHUFFLE,
        ),
        listOf(SettingsSectionKey.SPEED_PITCH, SettingsSectionKey.VOLUME_LEVELLING, SettingsSectionKey.PLAYBACK_MORE),
    ),
    SOUND_EQ(R.string.settings_group_sound_eq, R.string.settings_group_sound_eq_summary, listOf(SettingsRowKey.EQUALIZER)),
    OUTPUT(
        R.string.settings_group_output,
        R.string.settings_group_output_summary,
        listOf(SettingsRowKey.PAUSE_UNPLUGGED, SettingsRowKey.HEADPHONES_AUTOPLAY, SettingsRowKey.PICTURE_DELAY),
        listOf(SettingsSectionKey.OUTPUT_ENGINE, SettingsSectionKey.NOTIFICATION),
    ),
    LIBRARY(
        R.string.settings_group_library,
        R.string.settings_group_library_summary,
        listOf(SettingsRowKey.MUSIC_FOLDERS),
        listOf(SettingsSectionKey.LYRICS, SettingsSectionKey.OTHER_FOLDERS),
    ),
    VISUAL_PLAYBACK(
        R.string.settings_group_visual_playback,
        R.string.settings_group_visual_playback_summary,
        listOf(SettingsRowKey.AUTO_DIRECTOR, SettingsRowKey.STYLE_TIMER, SettingsRowKey.PRESET_MORPH),
    ),
    SOUND_REACTION(
        R.string.settings_group_sound_reaction,
        R.string.settings_group_sound_reaction_summary,
        listOf(SettingsRowKey.LIVE_INPUT, SettingsRowKey.OTHER_APPS_AUDIO, SettingsRowKey.NOTE_RESPONSE),
        listOf(SettingsSectionKey.ANALYSIS),
    ),
    SAFETY(
        R.string.settings_group_safety,
        R.string.settings_group_safety_summary,
        listOf(SettingsRowKey.FLASH_LIMITS, SettingsRowKey.REDUCED_MOTION),
    ),
    DISPLAYS(
        R.string.settings_group_displays,
        R.string.settings_group_displays_summary,
        listOf(SettingsRowKey.KEEP_SCREEN_ON_IN_VISUALS, SettingsRowKey.KEEP_SCREEN_ON_WITH_VISUAL),
        listOf(SettingsSectionKey.CONNECTED_DISPLAY, SettingsSectionKey.PICTURE_IN_PICTURE),
    ),
    WALLPAPER(
        R.string.settings_group_wallpaper,
        R.string.settings_group_wallpaper_summary,
        listOf(
            SettingsRowKey.LIVE_WALLPAPER,
            SettingsRowKey.DREAM_BRIGHT,
            SettingsRowKey.DREAM_CLOCK,
            SettingsRowKey.DREAM_NOW_PLAYING,
            SettingsRowKey.DREAM_OTHER_APPS,
        ),
    ),
    GESTURES(
        R.string.settings_group_gestures,
        R.string.settings_group_gestures_summary,
        listOf(SettingsRowKey.GESTURE_MAP),
        listOf(SettingsSectionKey.TOUCH),
    ),
    LOOK(
        R.string.settings_group_look,
        R.string.settings_group_look_summary,
        listOf(SettingsRowKey.START_SCREEN, SettingsRowKey.LOOK_THEME),
        listOf(SettingsSectionKey.LOOK_TEXT, SettingsSectionKey.LOOK_LAYOUT),
    ),
    PERFORMANCE(
        R.string.settings_group_performance,
        R.string.settings_group_performance_summary,
        listOf(SettingsRowKey.PERFORMANCE_MODE),
        listOf(SettingsSectionKey.PERF_DETAILS),
    ),
    EXPORT(
        R.string.settings_group_export,
        R.string.settings_group_export_summary,
        listOf(SettingsRowKey.OPEN_STUDIO_RENDER, SettingsRowKey.KEEP_RENDERS),
        listOf(SettingsSectionKey.RECORD),
    ),
    HELP_ABOUT(
        R.string.settings_group_help_about,
        R.string.settings_group_help_about_summary,
        listOf(SettingsRowKey.BACKUP),
        listOf(SettingsSectionKey.TUTORIALS, SettingsSectionKey.HELP_TOPICS, SettingsSectionKey.ABOUT),
    ),
    ;

    /** Every row on this tab's page, Basics first, then each section's in order. */
    val rows: List<SettingsRowKey>
        get() = basics + sections.flatMap { it.rows }

    /** A Material Symbol from [StoneIcon] where the app has one, else the Material icon. */
    val icon: ImageVector
        get() =
            when (this) {
                PLAYBACK -> StoneIcon.PLAY.vector
                SOUND_EQ -> Icons.Filled.Equalizer
                OUTPUT -> Icons.Filled.Headphones
                LIBRARY -> StoneIcon.LIBRARY.vector
                VISUAL_PLAYBACK -> StoneIcon.VISUALIZER.vector
                SOUND_REACTION -> StoneIcon.MICROPHONE.vector
                SAFETY -> Icons.Filled.Accessibility
                DISPLAYS -> StoneIcon.CAST.vector
                WALLPAPER -> Icons.Filled.Wallpaper
                GESTURES -> Icons.Filled.TouchApp
                LOOK -> Icons.Filled.Palette
                PERFORMANCE -> Icons.Filled.Speed
                EXPORT -> StoneIcon.STUDIO.vector
                HELP_ABOUT -> Icons.Filled.Backup
            }

    companion object {
        /**
         * The ten groups before the regroup (2026-11-11), by the tab most of their rows went to:
         * Behaviour's own rows split four ways, so it opens on Gestures & controls, the half of it
         * named in its title; Visuals opens on Visual playback; Tutorials and Backup became one tab.
         */
        private val FORMER_GROUPS =
            mapOf(
                "BEHAVIOUR" to GESTURES,
                "VISUALS" to VISUAL_PLAYBACK,
                "SOUND_INPUT" to SOUND_REACTION,
                "HELP" to HELP_ABOUT,
                "BACKUP_ABOUT" to HELP_ABOUT,
            )

        /**
         * Sections that were folded into a tab's Basics, by that tab: Equalizer is Sound &
         * equalizer, the gesture map is Gestures & controls, and the wallpaper and screen saver
         * are a tab of their own.
         */
        private val FORMER_SECTIONS =
            mapOf(
                "EQUALIZER" to SOUND_EQ,
                "GESTURES" to GESTURES,
                "WALLPAPER_DREAM" to WALLPAPER,
            )

        /** A tab by a saved name: today's, or a group's from before the regroup; null for none. */
        fun fromName(name: String?): SettingsGroupKey? = entries.firstOrNull { it.name == name } ?: FORMER_GROUPS[name]

        /** The tab that holds a section saved by name, today's or one folded into a tab's Basics. */
        fun ofSection(name: String?): SettingsGroupKey? =
            entries.firstOrNull { tab -> tab.sections.any { it.name == name } } ?: FORMER_SECTIONS[name]
    }
}

/** The sections below a tab's Basics, each named for its card, with the rows it holds. */
enum class SettingsSectionKey(
    @param:StringRes val titleRes: Int,
    val rows: List<SettingsRowKey>,
) {
    SPEED_PITCH(R.string.settings_section_speed_pitch, listOf(SettingsRowKey.SPEED_PITCH)),
    VOLUME_LEVELLING(R.string.settings_section_volume_levelling, listOf(SettingsRowKey.REPLAYGAIN)),
    PLAYBACK_MORE(R.string.settings_section_playback_more, listOf(SettingsRowKey.SKIP_SILENCE, SettingsRowKey.RESUME_LONG_TRACKS)),
    OUTPUT_ENGINE(R.string.settings_section_output_engine, listOf(SettingsRowKey.NATIVE_OUTPUT, SettingsRowKey.BIT_PERFECT)),
    NOTIFICATION(R.string.settings_section_notification, listOf(SettingsRowKey.NOTIFICATION_BUTTONS)),
    PICTURE_IN_PICTURE(R.string.behavior_group_pip, listOf(SettingsRowKey.PIP_AUTO_ENTER)),
    TOUCH(R.string.behavior_group_touch, listOf(SettingsRowKey.TOUCH_SMEAR, SettingsRowKey.TOUCH_TRANSFORM)),
    CONNECTED_DISPLAY(R.string.behavior_group_display, listOf(SettingsRowKey.CONNECTED_DISPLAY)),
    ANALYSIS(R.string.audio_group_analysis, listOf(SettingsRowKey.SOUND_TUNING)),
    LOOK_TEXT(R.string.look_group_text, listOf(SettingsRowKey.LOOK_TEXT)),
    LOOK_LAYOUT(R.string.look_group_layout, listOf(SettingsRowKey.LOOK_LAYOUT)),
    PERF_DETAILS(R.string.perf_details_title, listOf(SettingsRowKey.PERFORMANCE_DETAILS)),
    LYRICS(R.string.settings_tab_lyrics, listOf(SettingsRowKey.LYRICS)),
    OTHER_FOLDERS(R.string.settings_section_other_folders, listOf(SettingsRowKey.PRESET_FOLDER)),
    RECORD(R.string.settings_section_record, listOf(SettingsRowKey.RECORD_QUALITY)),
    TUTORIALS(R.string.settings_section_tutorials, listOf(SettingsRowKey.TUTORIALS)),
    HELP_TOPICS(R.string.help_topics_group, listOf(SettingsRowKey.HELP_TOPICS)),
    ABOUT(R.string.settings_tab_about, listOf(SettingsRowKey.ABOUT)),
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
    onPlayTutorial: (List<TourChapter>) -> Unit,
    onResetTutorials: () -> Unit,
    requestedGroup: SettingsGroupKey? = null,
    onRequestedGroupShown: () -> Unit = {},
) {
    var groupName by rememberSaveable { mutableStateOf<String?>(null) }
    // The open sections, as "GROUP/SECTION" names joined by ",", across every group: a group
    // stepped away from and back to opens as it was left.
    var openSections by rememberSaveable { mutableStateOf("") }
    val group = SettingsGroupKey.fromName(groupName)
    val listState = rememberLazyListState()
    // Another screen asked for a group (the empty library's "Choose a music folder" → Library):
    // open it once, then hand the request back so a later visit opens where the user left it.
    LaunchedEffect(requestedGroup) {
        if (requestedGroup != null) {
            groupName = requestedGroup.name
            onRequestedGroupShown()
        }
    }
    CloseOnBack(enabled = group != null) { groupName = null }
    Box(Modifier.fillMaxSize(), contentAlignment = Alignment.TopCenter) {
        Column(Modifier.widthIn(max = SettingsMaxWidth).fillMaxSize()) {
            if (group == null) {
                // The rows dissolve under the screen's heading instead of showing a half-cut line.
                LazyColumn(
                    Modifier.fillMaxSize().edgeFade(listState, horizontal = false),
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
                    val pageScroll = rememberScrollState()
                    Column(
                        Modifier
                            .fillMaxSize()
                            // Under the group's header too, where "Open on" showed cut in half.
                            .edgeFade(pageScroll, horizontal = false)
                            .verticalScroll(pageScroll)
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

/** What a row may need from the page it is drawn on. */
private class SettingsPageDeps(
    val viewModel: PlayerViewModel,
    val settings: SettingsViewModel,
    val behaviour: BehaviourPrefsHolder,
    val onOpenStudioRender: () -> Unit,
    val onPlayTutorial: (List<TourChapter>) -> Unit,
    val onResetTutorials: () -> Unit,
)

/** A tab's page: Basics, then its sections, each open or closed by [open]; every row by its key. */
@Composable
private fun SettingsGroupPage(
    group: SettingsGroupKey,
    viewModel: PlayerViewModel,
    open: (SettingsSectionKey) -> Boolean,
    onToggle: (SettingsSectionKey) -> Unit,
    onOpenStudioRender: () -> Unit,
    onPlayTutorial: (List<TourChapter>) -> Unit,
    onResetTutorials: () -> Unit,
) {
    val settings: SettingsViewModel = geodeViewModel()
    // BehaviourPrefs' rows sit on seven tabs now; one copy for the page, read once on IO.
    val behaviour = rememberBehaviourPrefs()
    val deps = SettingsPageDeps(viewModel, settings, behaviour, onOpenStudioRender, onPlayTutorial, onResetTutorials)
    SettingsBasicsCard {
        group.basics.forEach { SettingsRowContent(it, deps) }
    }
    group.sections.forEach { section ->
        SettingsSectionCard(
            title = stringResource(section.titleRes),
            expanded = open(section),
            onToggle = { onToggle(section) },
        ) {
            section.rows.forEach { SettingsRowContent(it, deps) }
        }
    }
}

/** One row by its key: whichever tab holds it, it draws and stores the same. */
@Composable
private fun SettingsRowContent(
    row: SettingsRowKey,
    deps: SettingsPageDeps,
) {
    val settings = deps.settings
    when (row) {
        SettingsRowKey.CROSSFADE,
        SettingsRowKey.FADE,
        SettingsRowKey.AUTO_RESUME,
        SettingsRowKey.SLEEP_TIMER,
        SettingsRowKey.SPEED_PITCH,
        SettingsRowKey.REPLAYGAIN,
        SettingsRowKey.SKIP_SILENCE,
        SettingsRowKey.RESUME_LONG_TRACKS,
        SettingsRowKey.PAUSE_UNPLUGGED,
        SettingsRowKey.NATIVE_OUTPUT,
        SettingsRowKey.BIT_PERFECT,
        SettingsRowKey.KEEP_SCREEN_ON_WITH_VISUAL,
        SettingsRowKey.PICTURE_DELAY,
        -> PlayerPrefsRow(row, settings)
        SettingsRowKey.QUEUE_END,
        SettingsRowKey.REMEMBER_SHUFFLE,
        SettingsRowKey.HEADPHONES_AUTOPLAY,
        SettingsRowKey.NOTIFICATION_BUTTONS,
        SettingsRowKey.KEEP_SCREEN_ON_IN_VISUALS,
        SettingsRowKey.DREAM_BRIGHT,
        SettingsRowKey.DREAM_CLOCK,
        SettingsRowKey.DREAM_NOW_PLAYING,
        SettingsRowKey.DREAM_OTHER_APPS,
        SettingsRowKey.GESTURE_MAP,
        SettingsRowKey.START_SCREEN,
        -> deps.behaviour.prefs?.let { BehaviourRow(row, it, deps.behaviour.update) }
        SettingsRowKey.EQUALIZER -> EqualizerSettings(settings)
        SettingsRowKey.MUSIC_FOLDERS -> MusicFoldersBasics()
        SettingsRowKey.LYRICS -> LyricsSection(deps.viewModel)
        SettingsRowKey.PRESET_FOLDER -> OtherFoldersSection()
        SettingsRowKey.AUTO_DIRECTOR -> AutoDirectorRows()
        SettingsRowKey.STYLE_TIMER -> StyleTimerRows(deps.viewModel)
        SettingsRowKey.PRESET_MORPH -> PresetMorphRow(settings)
        SettingsRowKey.LIVE_INPUT -> LiveInputRow(deps.viewModel)
        SettingsRowKey.OTHER_APPS_AUDIO -> OtherAppsAudioRow(deps.viewModel)
        SettingsRowKey.NOTE_RESPONSE -> NoteResponseGroup()
        SettingsRowKey.SOUND_TUNING -> SoundAnalysisSection(settings)
        SettingsRowKey.FLASH_LIMITS -> FlashLimitsRow()
        SettingsRowKey.REDUCED_MOTION -> ReducedMotionRow()
        SettingsRowKey.CONNECTED_DISPLAY -> ConnectedDisplaySection()
        SettingsRowKey.PIP_AUTO_ENTER -> PictureInPictureSection(settings)
        SettingsRowKey.LIVE_WALLPAPER -> LiveWallpaperRow()
        SettingsRowKey.TOUCH_SMEAR -> TouchSmearRow(settings)
        SettingsRowKey.TOUCH_TRANSFORM -> TouchTransformRow(settings)
        SettingsRowKey.LOOK_THEME -> LookBasics(settings)
        SettingsRowKey.LOOK_TEXT -> LookTextSection(settings)
        SettingsRowKey.LOOK_LAYOUT -> LookLayoutSection(settings)
        SettingsRowKey.PERFORMANCE_MODE -> PerformanceBasics()
        SettingsRowKey.PERFORMANCE_DETAILS -> PerformanceDetailsSection()
        SettingsRowKey.OPEN_STUDIO_RENDER -> OpenStudioRenderRow(deps.onOpenStudioRender)
        SettingsRowKey.KEEP_RENDERS -> KeepRendersRow()
        SettingsRowKey.RECORD_QUALITY -> RecordSection()
        SettingsRowKey.BACKUP -> BackupSection()
        SettingsRowKey.TUTORIALS -> TutorialSettingsTab(settings, onPlay = deps.onPlayTutorial, onReset = deps.onResetTutorials)
        SettingsRowKey.HELP_TOPICS -> HelpTopicsSection()
        SettingsRowKey.ABOUT -> AboutSection()
    }
}

/**
 * Export, loop render and their defaults belong to Studio › Render; Export & recording's Basics
 * points there, so someone who looks for them in Settings (where they used to be) still finds the
 * way. Render's "add it to My videos?" answer stays changeable beside it ([KeepRendersRow]), and
 * what a recorded take renders at is the tab's one section ([RecordSection]): a take renders in
 * the background with no Render page to choose on.
 */
@Composable
private fun OpenStudioRenderRow(onOpenStudioRender: () -> Unit) {
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
