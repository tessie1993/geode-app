package dev.geode.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.render.scene.MilkdropEngine
import kotlin.math.roundToInt

private val INTERVAL_RANGE =
    AutoVisualsPrefsStore.INTERVAL_SEC.first.toFloat()..AutoVisualsPrefsStore.INTERVAL_SEC.last.toFloat()

@Composable
internal fun AutoVisualsGroup(viewModel: PlayerViewModel) {
    val viz by viewModel.vizState.collectAsStateWithLifecycle()
    val nothingToPickFrom = !viz.randomIncludeStyles && !viz.randomIncludePresets && !viz.randomIncludeMilk
    Text(
        stringResource(R.string.autoviz_intro),
        style = MaterialTheme.typography.bodySmall,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
    )
    CrystalOverline(stringResource(R.string.autoviz_random_overline), color = MaterialTheme.colorScheme.onSurfaceVariant)
    Column {
        Text(
            stringResource(
                if (viz.randomEnabled) {
                    R.string.autoviz_random_running
                } else if (viz.vizPlaylistEnabled) {
                    R.string.autoviz_random_off_playlist
                } else {
                    R.string.autoviz_random_off_auto
                },
            ),
            style = MaterialTheme.typography.labelMedium,
            color = if (viz.randomEnabled) accentTextColor() else MaterialTheme.colorScheme.onSurfaceVariant,
        )
        Text(
            stringResource(R.string.autoviz_shape_hint),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
    Column {
        Text(stringResource(R.string.autoviz_switch_every, viz.randomIntervalSec), style = MaterialTheme.typography.labelMedium)
        CrystalSlider(
            value = viz.randomIntervalSec.toFloat(),
            onValueChange = { viewModel.setRandomInterval(it.roundToInt()) },
            valueRange = INTERVAL_RANGE,
        )
        LabeledSwitch(
            stringResource(R.string.autoviz_on_beat),
            checked = viz.randomOnBeat,
            onCheckedChange = viewModel::setRandomOnBeat,
        )
        Text(
            stringResource(R.string.autoviz_on_beat_hint),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
    Column {
        Text(stringResource(R.string.autoviz_pick_from), style = MaterialTheme.typography.labelMedium)
        LabeledSwitch(
            stringResource(R.string.autoviz_pick_styles),
            checked = viz.randomIncludeStyles,
            onCheckedChange = viewModel::setRandomIncludeStyles,
        )
        LabeledSwitch(
            stringResource(R.string.autoviz_pick_presets),
            checked = viz.randomIncludePresets,
            onCheckedChange = viewModel::setRandomIncludePresets,
        )
        if (MilkdropEngine.available) {
            LabeledSwitch(
                stringResource(R.string.autoviz_pick_milk),
                checked = viz.randomIncludeMilk,
                onCheckedChange = viewModel::setRandomIncludeMilk,
            )
        }
        Text(
            stringResource(
                if (nothingToPickFrom) R.string.autoviz_nothing_selected else R.string.autoviz_pick_hint,
            ),
            style = MaterialTheme.typography.bodySmall,
            color =
                if (nothingToPickFrom) {
                    MaterialTheme.colorScheme.error
                } else {
                    MaterialTheme.colorScheme.onSurfaceVariant
                },
        )
    }
    Column {
        LabeledSwitch(
            stringResource(R.string.autoviz_roll_colors),
            checked = viz.randomizeColors,
            onCheckedChange = viewModel::setRandomizeColors,
        )
        Text(
            stringResource(R.string.autoviz_roll_colors_hint),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
    CrystalOverline(stringResource(R.string.autoviz_playlist_overline), color = MaterialTheme.colorScheme.onSurfaceVariant)
    Column {
        LabeledSwitch(
            stringResource(R.string.autoviz_playlist_play),
            checked = viz.vizPlaylistEnabled,
            onCheckedChange = viewModel::setVizPlaylistEnabled,
        )
        Text(
            when {
                viz.randomEnabled -> stringResource(R.string.autoviz_playlist_random_running)
                viz.vizPlaylist.size >= 2 ->
                    pluralStringResource(
                        R.plurals.autoviz_playlist_count,
                        viz.vizPlaylist.size,
                        viz.vizPlaylist.size,
                    )
                else -> stringResource(R.string.autoviz_playlist_add_hint)
            },
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
    Column {
        Text(stringResource(R.string.autoviz_switch_every, viz.vizPlaylistIntervalSec), style = MaterialTheme.typography.labelMedium)
        CrystalSlider(
            value = viz.vizPlaylistIntervalSec.toFloat(),
            onValueChange = { viewModel.setVizPlaylistInterval(it.roundToInt()) },
            valueRange = INTERVAL_RANGE,
        )
        LabeledSwitch(
            stringResource(R.string.autoviz_wait_strong),
            checked = viz.vizPlaylistIntelligent,
            onCheckedChange = viewModel::setVizPlaylistIntelligent,
        )
        Text(
            stringResource(R.string.autoviz_wait_strong_hint),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}
