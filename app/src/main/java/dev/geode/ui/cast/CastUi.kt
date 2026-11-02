package dev.geode.ui.cast

import android.app.Activity
import androidx.annotation.OptIn
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.media3.cast.MediaRouteButton
import androidx.media3.common.util.UnstableApi
import dev.geode.R
import dev.geode.cast.CastController
import dev.geode.cast.CastState
import dev.geode.pro.PaywallTrigger
import dev.geode.pro.ProFeature
import dev.geode.ui.PlayerViewModel
import dev.geode.ui.geodeViewModel
import dev.geode.ui.pro.PaywallViewModel
import dev.geode.ui.theme.StoneIcon
import dev.geode.ui.theme.StoneIconArt
import dev.geode.ui.theme.floatingGlass

/**
 * The Cast button: Media3's route button, which lists the TVs on the network and starts a session.
 *
 * It does its own Pro check (UI lead, 2026-11-02): without Pro or the trial a tap opens the paywall
 * instead of the device list, over the same icon so the button looks the same to everyone. The
 * controller is built on first composition, so a session started here (or from the system's output
 * switcher afterwards) has someone listening for it; [CastController] checks Pro again then.
 */
@OptIn(UnstableApi::class)
@Composable
fun CastButton(modifier: Modifier = Modifier) {
    val context = LocalContext.current
    remember { CastController.get(context) }
    val paywall: PaywallViewModel = geodeViewModel()
    val gate by paywall.gate.collectAsStateWithLifecycle()
    Box(modifier) {
        MediaRouteButton()
        if (!gate.allows(ProFeature.CAST)) {
            Box(
                Modifier
                    .matchParentSize()
                    .clickable(
                        interactionSource = remember { MutableInteractionSource() },
                        indication = null,
                        onClickLabel = stringResource(R.string.cast_button_locked),
                    ) { paywall.open(PaywallTrigger.Feature(ProFeature.CAST)) },
            )
        }
    }
}

/**
 * The phone while it casts: a dimmed, simple remote (owner Q19). The TV plays the picture and the
 * sound; this shows where it goes and drives the music's player: previous, play/pause, next, the
 * next style ([onNextStyle], stepped the way the visual's swipe does) and Stop casting.
 *
 * The screen is dimmed to its lowest brightness while this is shown and given back when it goes.
 * Hosted full screen by the shell while [CastController.state] is [CastState.Casting].
 */
@Composable
fun CastRemote(
    controller: CastController,
    onNextStyle: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val state by controller.state.collectAsStateWithLifecycle()
    val player: PlayerViewModel = geodeViewModel()
    val ui by player.uiState.collectAsStateWithLifecycle()
    DimScreen()
    Box(
        modifier
            .fillMaxSize()
            .background(Color.Black.copy(alpha = DIM_ALPHA))
            .padding(24.dp),
        contentAlignment = Alignment.Center,
    ) {
        Column(
            Modifier
                .widthIn(max = 420.dp)
                .fillMaxWidth()
                .floatingGlass()
                .padding(24.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            val device = (state as? CastState.Casting)?.deviceName
            Text(
                text = device?.let { stringResource(R.string.cast_remote_to, it) } ?: stringResource(R.string.cast_remote_to_tv),
                style = MaterialTheme.typography.labelLarge,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Spacer(Modifier.height(8.dp))
            Text(
                text = ui.title?.takeIf { it.isNotBlank() } ?: stringResource(R.string.cast_stream_title),
                style = MaterialTheme.typography.titleLarge,
                textAlign = TextAlign.Center,
                maxLines = 2,
                overflow = TextOverflow.Ellipsis,
            )
            Spacer(Modifier.height(24.dp))
            Row(
                Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceEvenly,
                verticalAlignment = Alignment.CenterVertically,
            ) {
                RemoteButton(StoneIcon.PREVIOUS, R.string.cast_remote_previous) { player.previous() }
                RemoteButton(
                    if (ui.isPlaying) StoneIcon.PAUSE else StoneIcon.PLAY,
                    if (ui.isPlaying) R.string.cast_remote_pause else R.string.cast_remote_play,
                    large = true,
                ) { player.togglePlayPause() }
                RemoteButton(StoneIcon.NEXT, R.string.cast_remote_next) { player.next() }
            }
            Spacer(Modifier.height(16.dp))
            TextButton(onClick = onNextStyle) {
                StoneIconArt(StoneIcon.VISUALIZER, contentDescription = null)
                Spacer(Modifier.size(8.dp))
                Text(stringResource(R.string.cast_remote_next_style))
            }
            TextButton(onClick = { controller.stop() }) {
                StoneIconArt(StoneIcon.CLOSE, contentDescription = null)
                Spacer(Modifier.size(8.dp))
                Text(stringResource(R.string.cast_stop))
            }
        }
    }
}

@Composable
private fun RemoteButton(
    icon: StoneIcon,
    label: Int,
    large: Boolean = false,
    onClick: () -> Unit,
) {
    val size = if (large) 72.dp else 56.dp
    IconButton(onClick = onClick, modifier = Modifier.size(size)) {
        StoneIconArt(icon, contentDescription = stringResource(label), modifier = Modifier.size(size / 2))
    }
}

/** Turns the screen down to its lowest brightness for as long as the remote is shown. */
@Composable
private fun DimScreen() {
    val window = (LocalContext.current as? Activity)?.window ?: return
    DisposableEffect(window) {
        val before = window.attributes.screenBrightness
        window.attributes = window.attributes.apply { screenBrightness = DIM_BRIGHTNESS }
        onDispose {
            window.attributes = window.attributes.apply { screenBrightness = before }
        }
    }
}

private const val DIM_ALPHA = 0.85f
private const val DIM_BRIGHTNESS = 0.02f
