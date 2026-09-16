package dev.geode.ui.glass

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import dev.geode.R

/** The title bar from ref-05: close ×, centred title, ⋮ menu. */
@Composable
fun GlassTopBar(
    title: String,
    modifier: Modifier = Modifier,
    onClose: (() -> Unit)? = null,
    onMenu: (() -> Unit)? = null,
    closeDescription: String = stringResource(R.string.action_close),
    menuDescription: String = stringResource(R.string.action_more),
) {
    Box(modifier.fillMaxWidth().height(56.dp).padding(horizontal = 12.dp)) {
        if (onClose != null) {
            GlassBubbleButton(
                icon = GlassIcons.Close,
                contentDescription = closeDescription,
                onClick = onClose,
                size = 36.dp,
                modifier = Modifier.align(Alignment.CenterStart),
            )
        }
        Text(
            title,
            Modifier.align(Alignment.Center),
            style = MaterialTheme.typography.headlineSmall,
            color = GlassPalette.textPrimary,
        )
        if (onMenu != null) {
            GlassBubbleButton(
                icon = GlassIcons.More,
                contentDescription = menuDescription,
                onClick = onMenu,
                size = 36.dp,
                modifier = Modifier.align(Alignment.CenterEnd),
            )
        }
    }
}
