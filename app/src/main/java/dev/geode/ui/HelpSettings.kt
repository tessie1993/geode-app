package dev.geode.ui

import androidx.annotation.StringRes
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R

/**
 * One answerable question about the app.
 *
 * Written from the user's side of the screen — what a thing does and where it is — rather than
 * how it is built. Everything here is a question someone can actually arrive with; there is no
 * entry explaining what a preset is in the abstract.
 */
private enum class HelpTopic(
    @param:StringRes val titleRes: Int,
    @param:StringRes val bodyRes: Int,
) {
    LIBRARY(R.string.help_topic_library, R.string.help_topic_library_body),
    VISUALS(R.string.help_topic_visuals, R.string.help_topic_visuals_body),
    TOUCH(R.string.help_topic_touch, R.string.help_topic_touch_body),
    SAFETY(R.string.help_topic_safety, R.string.help_topic_safety_body),
    PERFORMANCE(R.string.help_topic_performance, R.string.help_topic_performance_body),
}

/**
 * Settings › Help.
 *
 * The walkthrough is not a first-run-only thing. It is the only place the app explains touch on
 * the visualizer at all, and someone who skipped it on day one is exactly the person who needs
 * to find it on day three — so it lives here permanently, replayable, next to the toggle that
 * decides whether a fresh install offers it.
 */
@Composable
internal fun HelpSettingsTab(
    settingsViewModel: SettingsViewModel,
    onStartTutorial: () -> Unit,
) {
    val gui by settingsViewModel.guiPrefs.collectAsStateWithLifecycle()
    SettingsTabColumn {
        item {
            SettingsGroup(stringResource(R.string.help_tutorial_group)) {
                Text(
                    stringResource(R.string.help_tutorial_body),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                CrystalButton(onClick = onStartTutorial, modifier = Modifier.fillMaxWidth()) {
                    Text(
                        stringResource(
                            if (gui.tutorialSeen) R.string.help_tutorial_replay else R.string.help_tutorial_start,
                        ),
                    )
                }
                Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        stringResource(R.string.help_tutorial_on_first_run),
                        style = MaterialTheme.typography.bodySmall,
                        modifier = Modifier.weight(1f),
                    )
                    Switch(
                        checked = gui.tutorialOnFirstRun,
                        onCheckedChange = { settingsViewModel.setGuiPrefs(gui.copy(tutorialOnFirstRun = it)) },
                    )
                }
            }
        }
        item {
            SettingsGroup(stringResource(R.string.help_topics_group)) {
                Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                    HelpTopic.entries.forEach { topic ->
                        Column(verticalArrangement = Arrangement.spacedBy(3.dp)) {
                            Text(
                                stringResource(topic.titleRes),
                                style = MaterialTheme.typography.titleSmall,
                                color = MaterialTheme.colorScheme.onSurface,
                            )
                            Text(
                                stringResource(topic.bodyRes),
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                        }
                    }
                }
            }
        }
    }
}
