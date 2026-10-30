package dev.geode.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.unit.dp
import dev.geode.render.scene.Macro
import dev.geode.render.scene.Macros
import dev.geode.render.scene.SceneParams
import kotlin.math.abs
import kotlin.math.roundToInt

/**
 * The first page of Customize: six macros, each one knob over several detail parameters.
 *
 * A macro is drawn only when the style on screen has something it can move. Dragging one writes
 * the real parameters through [onChange] (the panel's one undoable write path), so the sliders in
 * the other tabs move with it and the change is saved, undone and exported like any other edit.
 */
@Composable
internal fun MacrosTab(
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
    onSurprise: () -> Unit,
    onReset: () -> Unit,
) {
    val sceneId = LocalSceneId.current
    val query = LocalParamQuery.current.trim()
    Column {
        if (query.isBlank()) {
            Text(
                "Six big moves. Each one turns several of the detailed controls at once, and you " +
                    "can see them follow in the other tabs.",
                modifier = Modifier.padding(top = 8.dp, bottom = 4.dp),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                CrystalButton(compact = true, onClick = onSurprise) { Text("Surprise me") }
                CrystalButton(compact = true, filled = false, onClick = onReset) { Text("Reset macros") }
            }
        }
        Macro.entries
            .filter { Macros.targets(it, sceneId).isNotEmpty() }
            .filter { query.isBlank() || it.title.contains(query, ignoreCase = true) }
            .forEach { macro -> MacroControl(macro, p, sceneId, onChange) }
    }
}

@Composable
private fun MacroControl(
    macro: Macro,
    p: SceneParams,
    sceneId: String,
    onChange: (SceneParams) -> Unit,
) {
    // A drag is applied to the parameters it started from (see Macros.set), so dragging back to
    // where it began restores the look exactly. There is no "drag finished" callback to end the
    // gesture; instead the anchor holds for as long as the live params are still the ones this
    // control last wrote, and any other edit (another slider, undo, a preset) starts a new one.
    var anchor by remember(sceneId) { mutableStateOf<Pair<SceneParams, SceneParams>?>(null) }
    val position = Macros.position(p, macro, sceneId)
    val atNeutral = abs(position - macro.neutral) < NEUTRAL_EPSILON
    val shown = macroValueText(macro, position)
    Column(Modifier.fillMaxWidth().padding(top = 14.dp)) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Text(macro.title, style = MaterialTheme.typography.titleSmall, modifier = Modifier.weight(1f))
            Text(shown, style = MaterialTheme.typography.labelMedium, color = accentTextColor())
            if (!atNeutral) {
                TextButton(
                    onClick = { onChange(Macros.reset(p, listOf(macro))) },
                    modifier = Modifier.semantics { contentDescription = "Reset ${macro.title}" },
                ) { Text("Reset") }
            }
        }
        Text(
            macro.hint,
            style = MaterialTheme.typography.labelSmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        CrystalSlider(
            value = position,
            onValueChange = { to ->
                val held = anchor?.takeIf { it.second == p }?.first ?: p
                val out = Macros.set(held, macro, to, sceneId)
                anchor = held to out
                onChange(out)
            },
            modifier =
                Modifier
                    .fillMaxWidth()
                    .semantics {
                        contentDescription = macro.title
                        stateDescription = "${macro.lowLabel} to ${macro.highLabel}, $shown"
                    },
        )
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            Text(macro.lowLabel, style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
            Text(
                Macros.targets(macro, sceneId).joinToString(" · ") { it.key },
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.6f),
                maxLines = 1,
                modifier = Modifier.weight(1f).padding(horizontal = 8.dp),
                overflow = androidx.compose.ui.text.style.TextOverflow.Ellipsis,
            )
            Text(macro.highLabel, style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
    }
}

/** "As styled" at the neutral; a signed step for the bipolar macros, a percentage for the rest. */
internal fun macroValueText(
    macro: Macro,
    position: Float,
): String {
    if (abs(position - macro.neutral) < NEUTRAL_EPSILON) return "as styled"
    return if (macro.neutral > 0f) {
        val signed = ((position - macro.neutral) / (1f - macro.neutral) * PERCENT).roundToInt()
        "%+d".format(signed)
    } else {
        "${(position * PERCENT).roundToInt()}%"
    }
}

private const val NEUTRAL_EPSILON = 0.005f
private const val PERCENT = 100f
