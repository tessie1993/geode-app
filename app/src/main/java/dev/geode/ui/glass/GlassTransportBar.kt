package dev.geode.ui.glass

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import dev.geode.R

/** The bottom transport row from ref-05: library, previous, big play/pause, next, profile. */
@Composable
fun GlassTransportBar(
    playing: Boolean,
    onPlayPause: () -> Unit,
    onPrevious: () -> Unit,
    onNext: () -> Unit,
    modifier: Modifier = Modifier,
    onLibrary: (() -> Unit)? = null,
    onProfile: (() -> Unit)? = null,
) {
    Row(
        modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        if (onLibrary != null) {
            GlassBubbleButton(
                icon = GlassIcons.MusicNote,
                contentDescription = stringResource(R.string.action_open_library),
                onClick = onLibrary,
            )
        }
        GlassBubbleButton(
            icon = GlassIcons.Previous,
            contentDescription = stringResource(R.string.action_previous),
            onClick = onPrevious,
        )
        GlassPlayButton(
            icon = if (playing) GlassIcons.Pause else GlassIcons.Play,
            contentDescription = stringResource(if (playing) R.string.action_pause else R.string.action_play),
            onClick = onPlayPause,
        )
        GlassBubbleButton(
            icon = GlassIcons.Next,
            contentDescription = stringResource(R.string.action_next),
            onClick = onNext,
        )
        if (onProfile != null) {
            GlassBubbleButton(
                icon = GlassIcons.Profile,
                contentDescription = stringResource(R.string.glass_profile),
                onClick = onProfile,
            )
        }
    }
}
