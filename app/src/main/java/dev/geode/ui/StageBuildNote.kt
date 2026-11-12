package dev.geode.ui

import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import dev.geode.R

/**
 * A style that did not build: the renderer's report (the style, the stage, the driver's first lines;
 * native `core/viz/BuildReport`) shown as it came, until closed. A new report shows again.
 */
@Composable
internal fun StageBuildNote(
    report: String?,
    pip: Boolean,
) {
    var closed by remember(report) { mutableStateOf(false) }
    if (report == null || pip || closed) return
    AlertDialog(
        onDismissRequest = { closed = true },
        title = { Text(stringResource(R.string.stage_style_failed)) },
        text = {
            SelectionContainer {
                Text(
                    report,
                    style = MaterialTheme.typography.bodySmall,
                    fontFamily = FontFamily.Monospace,
                    modifier = Modifier.heightIn(max = 320.dp).verticalScroll(rememberScrollState()),
                )
            }
        },
        confirmButton = { TextButton(onClick = { closed = true }) { Text(stringResource(R.string.action_close)) } },
    )
}
