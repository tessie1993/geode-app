package dev.geode.ui

import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.res.stringResource
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.model.LibraryStartTab
import dev.geode.model.TrackTap
import dev.geode.model.WatermarkChoice
import dev.geode.ui.pro.PaywallViewModel

/**
 * Settings › Preferences › Watermark: where the wordmark goes on a video (tiled across the frame,
 * a corner, or an end card), and for Pro whether it goes on at all. Free and trial videos always
 * carry it, so their switch is not shown. The page only stores the choice; every render pins it
 * when it starts, and where the mark lands and what an end card becomes on a loop or a recording is
 * native (`core/viz/frame/ExportPassMath`).
 */
@Composable
internal fun WatermarkRows(settings: SettingsViewModel) {
    val paywall: PaywallViewModel = geodeViewModel()
    val gate by paywall.gate.collectAsStateWithLifecycle()
    val choice by settings.watermark.collectAsStateWithLifecycle()
    val required = gate.watermarksExports
    SettingsHint(stringResource(if (required) R.string.watermark_hint_free else R.string.watermark_hint_pro))
    if (!required) {
        SettingsSwitchRow(
            title = stringResource(R.string.watermark_pro_adds),
            checked = choice.proAdds,
            onCheckedChange = { on -> settings.updateWatermark { it.copy(proAdds = on) } },
        )
    }
    val marked = required || choice.proAdds
    SettingsChoiceRow(
        title = stringResource(R.string.watermark_placement),
        options = WatermarkChoice.PLACEMENTS,
        selected = choice.placement,
        label = { stringResource(placementLabel(it)) },
        onSelect = { placement -> settings.updateWatermark { it.copy(placement = placement) } },
        enabled = marked,
    )
    if (choice.placement == WatermarkChoice.CORNER) {
        SettingsChoiceRow(
            title = stringResource(R.string.watermark_corner),
            options = WatermarkChoice.CORNERS,
            selected = choice.corner,
            label = { stringResource(cornerLabel(it)) },
            onSelect = { corner -> settings.updateWatermark { it.copy(corner = corner) } },
            enabled = marked,
        )
    }
    if (choice.placement == WatermarkChoice.END_CARD) SettingsHint(stringResource(R.string.watermark_end_card_hint))
}

private fun placementLabel(placement: Int): Int =
    when (placement) {
        WatermarkChoice.CORNER -> R.string.watermark_placement_corner
        WatermarkChoice.END_CARD -> R.string.watermark_placement_end_card
        else -> R.string.watermark_placement_tiled
    }

private fun cornerLabel(corner: Int): Int =
    when (corner) {
        WatermarkChoice.TOP_LEFT -> R.string.watermark_corner_top_left
        WatermarkChoice.TOP_RIGHT -> R.string.watermark_corner_top_right
        WatermarkChoice.BOTTOM_LEFT -> R.string.watermark_corner_bottom_left
        else -> R.string.watermark_corner_bottom_right
    }

/**
 * Settings › Preferences › Library: the tab and the order Library opens on, and what a tap on a
 * track does. Each default is how Library behaved before the choice existed (Tracks, by title,
 * play with its list), so an existing install opens exactly as it did.
 */
@Composable
internal fun LibraryChoiceRows(settings: SettingsViewModel) {
    val general by settings.general.collectAsStateWithLifecycle()
    SettingsChoiceRow(
        title = stringResource(R.string.prefs_library_start_tab),
        options = LibraryStartTab.entries,
        selected = general.libraryStartTab,
        label = { stringResource(it.labelRes()) },
        onSelect = { settings.updateGeneral { g -> g.copy(libraryStartTab = it) } },
    )
    SettingsChoiceRow(
        title = stringResource(R.string.prefs_library_sort),
        options = LibrarySort.entries,
        selected = LibrarySort.fromName(general.librarySort) ?: LibrarySort.TITLE,
        label = { stringResource(it.labelRes) },
        onSelect = { settings.updateGeneral { g -> g.copy(librarySort = it.name) } },
    )
    SettingsHint(stringResource(R.string.prefs_library_sort_hint))
    SettingsChoiceRow(
        title = stringResource(R.string.prefs_track_tap),
        options = TrackTap.entries,
        selected = general.trackTap,
        label = { stringResource(it.labelRes()) },
        onSelect = { settings.updateGeneral { g -> g.copy(trackTap = it) } },
    )
    SettingsHint(stringResource(R.string.prefs_track_tap_hint))
}

/** Settings › Preferences › Player & mini player: the A–B loop button, and previous on the floating mini player. */
@Composable
internal fun PlayerButtonRows(settings: SettingsViewModel) {
    val general by settings.general.collectAsStateWithLifecycle()
    SettingsSwitchRow(
        title = stringResource(R.string.prefs_show_ab_loop),
        summary = stringResource(R.string.prefs_show_ab_loop_hint),
        checked = general.showAbLoop,
        onCheckedChange = { settings.updateGeneral { g -> g.copy(showAbLoop = it) } },
    )
    SettingsSwitchRow(
        title = stringResource(R.string.prefs_mini_player_previous),
        summary = stringResource(R.string.prefs_mini_player_previous_hint),
        checked = general.miniPlayerPrevious,
        onCheckedChange = { settings.updateGeneral { g -> g.copy(miniPlayerPrevious = it) } },
    )
}

/**
 * Settings › Preferences › Icons on the visual: ‹ › and Record, for someone who steps and records
 * by gesture and wants the picture clear. Only the icons go; the gestures stay the gesture map's.
 */
@Composable
internal fun StageIconRows(settings: SettingsViewModel) {
    val general by settings.general.collectAsStateWithLifecycle()
    SettingsSwitchRow(
        title = stringResource(R.string.prefs_stage_arrows),
        summary = stringResource(R.string.prefs_stage_arrows_hint),
        checked = general.stageArrows,
        onCheckedChange = { settings.updateGeneral { g -> g.copy(stageArrows = it) } },
    )
    SettingsSwitchRow(
        title = stringResource(R.string.prefs_stage_record),
        summary = stringResource(R.string.prefs_stage_record_hint),
        checked = general.stageRecord,
        onCheckedChange = { settings.updateGeneral { g -> g.copy(stageRecord = it) } },
    )
}

private fun LibraryStartTab.labelRes(): Int =
    when (this) {
        LibraryStartTab.TRACKS -> R.string.library_tab_tracks
        LibraryStartTab.ALBUMS -> R.string.library_tab_albums
        LibraryStartTab.ARTISTS -> R.string.library_tab_artists
        LibraryStartTab.FOLDERS -> R.string.library_tab_folders
        LibraryStartTab.PLAYLISTS -> R.string.library_tab_playlists
    }

private fun TrackTap.labelRes(): Int =
    when (this) {
        TrackTap.PLAY_LIST -> R.string.prefs_track_tap_play_list
        TrackTap.PLAY_ALONE -> R.string.prefs_track_tap_play_alone
        TrackTap.PLAY_NEXT -> R.string.prefs_track_tap_play_next
        TrackTap.ADD_TO_QUEUE -> R.string.prefs_track_tap_add_to_queue
    }
