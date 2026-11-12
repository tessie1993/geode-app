package dev.geode.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.defaultMinSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.Button
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.model.TourChapter
import dev.geode.model.spots
import dev.geode.ui.theme.GeodeSpacing
import dev.geode.ui.theme.StoneIcon
import dev.geode.ui.theme.StoneIconArt
import dev.geode.ui.tour.titleRes

/**
 * Settings › Tutorials: the tour's chapters, replayable on their own, with a tick for each one seen.
 *
 * Someone who skipped the tour on day one is the person who needs it on day three, so it lives
 * here for good. A replay takes the user to each screen in turn and changes no tick; Reset all
 * clears them for the next launch and lets every gesture tip speak once more.
 */
@Composable
internal fun TutorialSettingsTab(
    settingsViewModel: SettingsViewModel,
    onPlay: (List<TourChapter>) -> Unit,
    onReset: () -> Unit,
) {
    val appState by settingsViewModel.appState.collectAsStateWithLifecycle()
    val done = appState.tourProgress.done
    val chapters = TourChapter.entries
    var resetNote by rememberSaveable { mutableStateOf(false) }
    Column(
        verticalArrangement = Arrangement.spacedBy(GeodeSpacing.S),
    ) {
        Text(
            stringResource(R.string.tutorials_group),
            Modifier.semantics { heading() },
            style = MaterialTheme.typography.titleSmall,
            color = MaterialTheme.colorScheme.onSurface,
        )
        Text(
            stringResource(R.string.tutorials_progress, done.size, chapters.size),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        LinearProgressIndicator(
            progress = { done.size / chapters.size.toFloat() },
            modifier = Modifier.fillMaxWidth().height(4.dp),
            color = MaterialTheme.colorScheme.primary,
            trackColor = MaterialTheme.colorScheme.primary.copy(alpha = 0.15f),
        )
        Button(onClick = { onPlay(chapters) }, modifier = Modifier.fillMaxWidth()) {
            Text(stringResource(R.string.tutorials_play_all))
        }
        Column {
            chapters.forEach { chapter ->
                ChapterRow(chapter, chapter in done, onClick = { onPlay(listOf(chapter)) })
            }
        }
        TextButton(
            onClick = {
                onReset()
                resetNote = true
            },
            modifier = Modifier.fillMaxWidth(),
        ) { Text(stringResource(R.string.tutorials_reset)) }
        if (resetNote) {
            Text(
                stringResource(R.string.tutorials_reset_done),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.primary,
            )
        }
    }
}

@Composable
private fun ChapterRow(
    chapter: TourChapter,
    done: Boolean,
    onClick: () -> Unit,
) {
    val cs = MaterialTheme.colorScheme
    val state = stringResource(if (done) R.string.tutorials_chapter_done else R.string.tutorials_chapter_not_done)
    Row(
        Modifier
            .fillMaxWidth()
            .defaultMinSize(minHeight = GeodeSpacing.TouchTarget)
            .clip(MaterialTheme.shapes.medium)
            .clickable(role = Role.Button, onClick = onClick)
            .semantics { stateDescription = state }
            .padding(horizontal = GeodeSpacing.S, vertical = GeodeSpacing.S),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(GeodeSpacing.M),
    ) {
        Box(
            Modifier
                .size(28.dp)
                .clip(CircleShape)
                .background(if (done) cs.primary else Color.Transparent)
                .border(1.5.dp, if (done) cs.primary else cs.outline, CircleShape),
            contentAlignment = Alignment.Center,
        ) {
            if (done) StoneIconArt(StoneIcon.CHECK, null, Modifier.size(18.dp), tint = cs.onPrimary)
        }
        Column(Modifier.weight(1f)) {
            Text(stringResource(chapter.titleRes), style = MaterialTheme.typography.titleSmall, color = cs.onSurface)
            Text(
                pluralStringResource(R.plurals.tour_chapter_pointers, chapter.spots.size, chapter.spots.size),
                style = MaterialTheme.typography.bodySmall,
                color = cs.onSurfaceVariant,
            )
        }
        StoneIconArt(StoneIcon.PLAY, null, Modifier.size(20.dp), tint = cs.primary)
    }
}
