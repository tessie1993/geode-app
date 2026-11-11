package dev.geode.ui.cast

import android.widget.Toast
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
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
import kotlin.math.floor

/**
 * "Show on a computer" in Visuals › Settings: one row beside the visual's other options, the ⚙ next to the Cast
 * icon. Not in the Cast icon itself: that is Media3's route button, whose device list the app can't add to, and
 * a fifth stage icon would crowd the four the visual already carries. A tap starts the share ([ComputerLink]) and
 * opens [ComputerLinkSheet]; while it runs a tap reopens the sheet. Without Pro the row opens the paywall, as the
 * Cast icon does.
 */
@Composable
internal fun ComputerLinkRow(modifier: Modifier = Modifier) {
    val context = LocalContext.current
    val paywall: PaywallViewModel = geodeViewModel()
    val gate by paywall.gate.collectAsStateWithLifecycle()
    val link by ComputerLink.state.collectAsStateWithLifecycle()
    var sheet by rememberSaveable { mutableStateOf(false) }
    val locked = !gate.allows(ProFeature.CAST)
    // An ended share closes the sheet; CastHost words why.
    LaunchedEffect(link) { if (!link.running) sheet = false }
    Row(
        modifier
            .fillMaxWidth()
            .heightIn(min = GeodeSpacing.TouchTarget)
            .clickable(role = Role.Button) {
                when {
                    link.running -> sheet = true
                    locked || ComputerLink.start(context) -> paywall.open(PaywallTrigger.Feature(ProFeature.CAST))
                    else -> sheet = ComputerLink.state.value.running
                }
            },
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(GeodeSpacing.S),
    ) {
        Text(stringResource(R.string.link_row), Modifier.weight(1f), style = MaterialTheme.typography.bodyMedium)
        Text(
            stringResource(linkRowValue(link, locked)),
            style = MaterialTheme.typography.labelMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        if (locked) ProBadge()
    }
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

/**
 * The live share: the address as text (selectable, to copy) and as a QR code made on the phone, whether it is
 * starting or live, the one line on who can watch (plain HTTP on the LAN: anyone on this Wi-Fi with the link),
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
                LinkQrCode(state.url)
                SelectionContainer {
                    Text(state.url, style = MaterialTheme.typography.bodyLarge.copy(fontFamily = FontFamily.Monospace))
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
