package dev.geode.ui.cast

import android.app.Activity
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.widget.Toast
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Computer
import androidx.compose.material.icons.outlined.Computer
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.Text
import androidx.compose.material3.ripple
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.cast.ComputerLink
import dev.geode.cast.LinkState
import dev.geode.engine.bridge.GeodeNative
import dev.geode.pro.PaywallTrigger
import dev.geode.pro.ProFeature
import dev.geode.ui.CrystalButton
import dev.geode.ui.geodeViewModel
import dev.geode.ui.pro.PaywallViewModel
import dev.geode.ui.pro.ProBadge
import dev.geode.ui.theme.GeodeSpacing
import dev.geode.ui.theme.StoneHapticCue
import dev.geode.ui.theme.performStoneHaptic
import kotlinx.coroutines.delay
import kotlin.math.floor

/**
 * "Show on a computer" in Visuals › Settings: one row beside the visual's other options; the stage carries the same
 * door as an icon beside Cast ([ComputerLinkButton]). Not inside the Cast icon itself: that is Media3's route
 * button, whose device list the app can't add to. A tap starts the share ([ComputerLink]) and
 * opens [ComputerLinkSheet]; while it runs a tap reopens the sheet. Without Pro the row opens the paywall, as the
 * Cast icon does.
 */
@Composable
internal fun ComputerLinkRow(modifier: Modifier = Modifier) {
    ComputerLinkDoor { door ->
        Row(
            modifier
                .fillMaxWidth()
                .heightIn(min = GeodeSpacing.TouchTarget)
                .clickable(role = Role.Button, onClick = door.tap),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(GeodeSpacing.S),
        ) {
            Text(stringResource(R.string.link_row), Modifier.weight(1f), style = MaterialTheme.typography.bodyMedium)
            Text(
                stringResource(linkRowValue(door.link, door.locked)),
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            if (door.locked) ProBadge()
        }
    }
}

/**
 * "Share to PC" on the stage, beside the Cast icon (owner: "make the button for share to pc in player next to cast
 * option"): the same door as Visuals › Settings' row. A tap starts the share and opens the sheet with the link, Copy
 * and Share; while it runs the icon is filled and a tap reopens the sheet. Without Pro or the trial it opens the
 * paywall, as Cast does. [enabled] follows the stage icons' fade: faded, it takes no taps and TalkBack skips it.
 */
@Composable
internal fun ComputerLinkButton(
    enabled: Boolean,
    modifier: Modifier = Modifier,
) {
    ComputerLinkDoor { door ->
        val label = stringResource(linkButtonLabel(door.link, door.locked))
        val view = LocalView.current
        Box(
            modifier
                .size(GeodeSpacing.TouchTarget)
                .then(
                    if (enabled) {
                        Modifier.clickable(
                            interactionSource = remember { MutableInteractionSource() },
                            indication = ripple(bounded = false, radius = GeodeSpacing.TouchTarget / 2),
                            role = Role.Button,
                        ) {
                            view.performStoneHaptic(StoneHapticCue.TAP)
                            door.tap()
                        }
                    } else {
                        Modifier.clearAndSetSemantics {}
                    },
                ),
            contentAlignment = Alignment.Center,
        ) {
            Icon(
                if (door.link.running) Icons.Filled.Computer else Icons.Outlined.Computer,
                label,
                tint = LocalContentColor.current,
            )
        }
    }
}

/** What a door to the share draws from: the share's state, whether Pro locks it, and its tap. */
internal class LinkDoor(
    val link: LinkState,
    val locked: Boolean,
    val tap: () -> Unit,
)

/**
 * One door to "Show on a computer" (Visuals › Settings' row, the stage's icon beside Cast, the Player's tile):
 * [content] draws it from a [LinkDoor]; this keeps the gate, the share's state and its sheet. A tap starts the
 * share and opens the sheet, reopens it while sharing, or opens the paywall without Pro. An ended share closes the
 * sheet; [CastHost] words why.
 */
@Composable
internal fun ComputerLinkDoor(content: @Composable (LinkDoor) -> Unit) {
    val context = LocalContext.current
    val paywall: PaywallViewModel = geodeViewModel()
    val gate by paywall.gate.collectAsStateWithLifecycle()
    val link by ComputerLink.state.collectAsStateWithLifecycle()
    var sheet by rememberSaveable { mutableStateOf(false) }
    val locked = !gate.allows(ProFeature.CAST)
    LaunchedEffect(link) { if (!link.running) sheet = false }
    content(LinkDoor(link, locked) { sheet = tapComputerLink(context, link, locked, paywall) })
    if (sheet) {
        ComputerLinkSheet(
            state = link,
            onStop = {
                ComputerLink.stop(context)
                sheet = false
            },
            onDismiss = { sheet = false },
        )
    }
}

/** A tap on either door; true when the sheet should open (the share runs, or just started). */
private fun tapComputerLink(
    context: Context,
    link: LinkState,
    locked: Boolean,
    paywall: PaywallViewModel,
): Boolean =
    when {
        link.running -> true
        locked || ComputerLink.start(context) -> {
            paywall.open(PaywallTrigger.Feature(ProFeature.CAST))
            false
        }
        else -> ComputerLink.state.value.running
    }

/** Puts the link on the clipboard; Android 13 and later confirm a copy on screen themselves. */
private fun copyLink(
    context: Context,
    url: String,
) {
    val clipboard = context.getSystemService(ClipboardManager::class.java) ?: return
    clipboard.setPrimaryClip(ClipData.newPlainText(context.getString(R.string.link_row), url))
}

/** Hands the link to another app (a message, a mail, Nearby Share) to open it on the computer. */
private fun shareLink(
    context: Context,
    url: String,
) {
    val send =
        Intent(Intent.ACTION_SEND)
            .setType("text/plain")
            .putExtra(Intent.EXTRA_TEXT, url)
    val chooser = Intent.createChooser(send, context.getString(R.string.link_share_title))
    if (context !is Activity) chooser.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
    runCatching { context.startActivity(chooser) }
        .onFailure { Toast.makeText(context, R.string.link_share_none, Toast.LENGTH_SHORT).show() }
}

/**
 * The live share: the link's short name, the whole link as a QR code made on the phone (for a device that scans),
 * and for a computer, which has no scanner, the short link with no token as text (selectable, to copy; its host
 * written as one number, not the dotted address) and the code its page asks for, read again every second since a
 * code is spent once used and replaced after a few minutes (native); whether it is starting or live, who can watch
 * and what is encrypted (the WebRTC media is; the plain-http link, page and fallback feed are not),
 * where the sound plays, and Stop. Dismissing the sheet keeps the share running; the notification and the row
 * stop it too.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
internal fun ComputerLinkSheet(
    state: LinkState,
    onStop: () -> Unit,
    onDismiss: () -> Unit,
) {
    ModalBottomSheet(onDismissRequest = onDismiss) {
        Column(
            Modifier.fillMaxWidth().padding(horizontal = 20.dp, vertical = 8.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Text(stringResource(R.string.link_row), style = MaterialTheme.typography.titleMedium)
            Text(
                stringResource(R.string.link_sheet_how),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            if (state is LinkState.Live) {
                val context = LocalContext.current
                if (state.label.isNotEmpty()) {
                    Text(stringResource(R.string.link_sheet_label, state.label), style = MaterialTheme.typography.titleSmall)
                }
                LinkQrCode(state.url)
                val typed = state.typedUrl.ifEmpty { state.url }
                val code by produceState("", state) {
                    while (true) {
                        value = ComputerLink.pairCode()
                        delay(CODE_REFRESH_MS)
                    }
                }
                Text(stringResource(R.string.link_sheet_typed), style = MaterialTheme.typography.bodySmall)
                SelectionContainer {
                    Text(typed, style = MaterialTheme.typography.bodyMedium.copy(fontFamily = FontFamily.Monospace))
                }
                if (code.isNotEmpty()) {
                    Text(stringResource(R.string.link_sheet_code), style = MaterialTheme.typography.bodySmall)
                    Text(code, style = MaterialTheme.typography.headlineMedium.copy(fontFamily = FontFamily.Monospace))
                }
                Text(
                    stringResource(R.string.link_sheet_address),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(GeodeSpacing.S)) {
                    CrystalButton(onClick = { copyLink(context, typed) }, modifier = Modifier.weight(1f), filled = false) {
                        Text(stringResource(R.string.link_copy))
                    }
                    CrystalButton(onClick = { shareLink(context, typed) }, modifier = Modifier.weight(1f), filled = false) {
                        Text(stringResource(R.string.link_share))
                    }
                }
            } else {
                CircularProgressIndicator(Modifier.padding(GeodeSpacing.L))
            }
            linkSheetStatus(state)?.let { Text(stringResource(it), style = MaterialTheme.typography.bodyMedium) }
            Text(
                stringResource(R.string.link_sheet_sound),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Text(
                stringResource(R.string.link_sheet_open),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Text(
                stringResource(R.string.link_sheet_plain),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            CrystalButton(onClick = onStop, modifier = Modifier.fillMaxWidth()) {
                Text(stringResource(R.string.link_stop))
            }
        }
    }
}

/**
 * The link as a QR code, encoded on the phone (native, core/util/QrCode: byte mode, level M), drawn dark on
 * light with the four-module quiet zone scanners need, whatever the theme. Modules snap to whole pixels.
 */
@Composable
private fun LinkQrCode(url: String) {
    val modules = remember(url) { ByteArray(GeodeNative.QR_MAX_MODULES) }
    val side = remember(url) { GeodeNative.qrEncode(url, modules) }
    if (side <= 0) return
    val label = stringResource(R.string.link_qr_description)
    Canvas(
        Modifier
            .size(QR_SIZE)
            .background(Color.White, RoundedCornerShape(8.dp))
            .semantics { contentDescription = label },
    ) {
        val total = side + 2 * QUIET_ZONE
        val cell = floor(size.minDimension / total)
        val origin = (size.minDimension - cell * total) / 2f + cell * QUIET_ZONE
        for (i in 0 until side * side) {
            if (modules[i].toInt() == 0) continue
            drawRect(Color.Black, Offset(origin + (i % side) * cell, origin + (i / side) * cell), Size(cell, cell))
        }
    }
}

/**
 * Says once why a share ended, wherever the app is (drawn from [CastHost]): a failure, a network change, the
 * trial's end. A share that ends in the background waits for the app.
 */
@Composable
internal fun ComputerLinkHost() {
    val context = LocalContext.current
    val link by ComputerLink.state.collectAsStateWithLifecycle()
    LaunchedEffect(link) {
        val ended = link as? LinkState.Ended ?: return@LaunchedEffect
        Toast.makeText(context, linkEndedMessage(ended.reason), Toast.LENGTH_LONG).show()
        ComputerLink.shown()
    }
}

private val QR_SIZE = 216.dp
private const val QUIET_ZONE = 4
private const val CODE_REFRESH_MS = 1_000L
