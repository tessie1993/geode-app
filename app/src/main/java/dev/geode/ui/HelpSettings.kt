package dev.geode.ui

import androidx.annotation.StringRes
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
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

/** Settings › Help: answers to the questions people actually arrive with. The tour is in Tutorials. */
@Composable
internal fun HelpSettingsTab() {
    SettingsTabColumn {
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
