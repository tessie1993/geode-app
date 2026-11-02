package dev.geode.ui.cast

import androidx.annotation.OptIn
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Box
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.media3.cast.MediaRouteButton
import androidx.media3.common.util.UnstableApi
import dev.geode.R
import dev.geode.cast.CastController
import dev.geode.cast.CastMirror
import dev.geode.pro.PaywallTrigger
import dev.geode.pro.ProFeature
import dev.geode.ui.PlayerViewModel
import dev.geode.ui.geodeViewModel
import dev.geode.ui.pro.PaywallViewModel
import kotlinx.coroutines.flow.map

/**
 * The Cast button: Media3's route button, which lists the TVs on the network and starts a session.
 *
 * It does its own Pro check (UI lead, 2026-11-02): without Pro or the trial a tap opens the paywall
 * instead of the device list, over the same icon so the button looks the same to everyone. The
 * controller is built on first composition, so a session started here (or from the system's output
 * switcher afterwards) has someone listening for it; [CastController] checks Pro again then.
 *
 * It also hands [CastMirror] the stage's live look (owner: the TV mirrors exactly what the phone's
 * visual shows, a style only previewed included). Casting changes nothing else in the app: no
 * remote screen, no dimming.
 */
@OptIn(UnstableApi::class)
@Composable
fun CastButton(modifier: Modifier = Modifier) {
    val context = LocalContext.current
    remember { CastController.get(context) }
    val paywall: PaywallViewModel = geodeViewModel()
    val gate by paywall.gate.collectAsStateWithLifecycle()
    val player: PlayerViewModel = geodeViewModel()
    LaunchedEffect(player) { CastMirror.follow(player.vizState.map { CastMirror.Look(it.sceneId, it.params) }) }
    Box(modifier) {
        MediaRouteButton()
        if (!gate.allows(ProFeature.CAST)) {
            // Over the route button, so TalkBack must hear this one: a label and a button role.
            val label = stringResource(R.string.cast_button_locked)
            Box(
                Modifier
                    .matchParentSize()
                    .semantics { contentDescription = label }
                    .clickable(
                        interactionSource = remember { MutableInteractionSource() },
                        indication = null,
                        role = Role.Button,
                    ) { paywall.open(PaywallTrigger.Feature(ProFeature.CAST)) },
            )
        }
    }
}
