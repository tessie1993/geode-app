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
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
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
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.model.TutorialChapter
import dev.geode.ui.theme.GeodeGlass
import dev.geode.ui.theme.GeodeSpacing
import dev.geode.ui.theme.StoneIcon
import dev.geode.ui.theme.StoneIconArt

/**
 * Settings › Tutorials: every chapter of the tour, replayable on its own, with a tick for each
 * one finished.
 *
 * The tour is the only place some things are explained at all (touch on the visualizer, Events),
 * and someone who skipped it on day one is the person who needs it on day three, so it lives
 * here for good. A replay never re-arms the first-run offer; Reset all does, for the next launch.
 */
@Composable
internal fun TutorialSettingsTab(
    settingsViewModel: SettingsViewModel,
    onPlay: (List<TutorialChapter>) -> Unit,
    onReset: () -> Unit,
) {
    val gui by settingsViewModel.guiPrefs.collectAsStateWithLifecycle()
    val done = gui.tutorialProgress.done
    var resetNote by rememberSaveable { mutableStateOf(false) }
    SettingsTabColumn {
        item {
            SettingsGroup(stringResource(R.string.tutorials_group)) {
                Text(
                    stringResource(R.string.tutorials_progress, done.size, TutorialChapter.entries.size),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                LinearProgressIndicator(
                    progress = { done.size / TutorialChapter.entries.size.toFloat() },
                    modifier = Modifier.fillMaxWidth().height(4.dp),
                    color = MaterialTheme.colorScheme.primary,
                    trackColor = MaterialTheme.colorScheme.primary.copy(alpha = 0.15f),
                )
                CrystalButton(onClick = { onPlay(TutorialChapter.entries) }, modifier = Modifier.fillMaxWidth()) {
                    Text(stringResource(R.string.tutorials_play_all))
                }
                Column {
                    TutorialChapter.entries.forEach { chapter ->
                        ChapterRow(chapter, chapter in done, onClick = { onPlay(listOf(chapter)) })
                    }
                }
                CrystalButton(
                    onClick = {
                        onReset()
                        resetNote = true
                    },
                    filled = false,
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
    }
}

@Composable
private fun ChapterRow(
    chapter: TutorialChapter,
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
                .border(1.5.dp, if (done) cs.primary else Color.White.copy(alpha = GeodeGlass.EDGE * 3), CircleShape),
            contentAlignment = Alignment.Center,
        ) {
            if (done) StoneIconArt(StoneIcon.CHECK, null, Modifier.size(18.dp), tint = cs.onPrimary)
        }
        Column(Modifier.weight(1f)) {
            Text(stringResource(chapter.titleRes), style = MaterialTheme.typography.titleSmall, color = cs.onSurface)
            Text(
                pluralStringResource(R.plurals.tutorials_chapter_cards, chapter.cards, chapter.cards),
                style = MaterialTheme.typography.bodySmall,
                color = cs.onSurfaceVariant,
            )
        }
        StoneIconArt(StoneIcon.PLAY, null, Modifier.size(20.dp), tint = cs.primary)
    }
}
