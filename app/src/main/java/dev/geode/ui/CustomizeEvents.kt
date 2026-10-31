package dev.geode.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import dev.geode.render.EventRecipe
import dev.geode.render.ReactionPreset
import dev.geode.render.ReactionRoute
import dev.geode.render.scene.BuildStyleReactionsUseCase
import dev.geode.render.scene.SceneParams

/**
 * The second page of Customize: what happens when something happens in the music.
 *
 * Ready-made [EventRecipe]s come first, as one-tap chips; the routes they add show up in the
 * list under them, where each one can be switched off, tuned or removed, and "Add" builds one
 * from scratch. Everything here is the reaction matrix the style DNA already plays, so turning a
 * recipe on is the same edit as adding its routes by hand and is saved the same way.
 */
@Composable
internal fun EventsTab(
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
    reactions: List<ReactionRoute>?,
    onReactionsChange: (List<ReactionRoute>?) -> Unit,
    onRerollReactions: () -> Unit,
) {
    val sceneId = LocalSceneId.current
    val searching = LocalParamQuery.current.isNotBlank()
    Column {
        if (!searching) {
            Text(
                "When something happens in the music, something moves. Tap a reaction to add it; " +
                    "tap it again to take it off.",
                modifier = Modifier.padding(top = 8.dp, bottom = 4.dp),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            val dna = remember(sceneId, p.reactSeed) { BuildStyleReactionsUseCase()(sceneId, p.reactSeed) }
            val routes = reactions ?: dna
            // One list for the chips and the slot note, so the note only ever speaks of chips on screen.
            val shown = remember(sceneId) { EventRecipe.entries.filter { it.routesFor(sceneId).isNotEmpty() } }
            FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(2.dp)) {
                shown.forEach { recipe ->
                    val on = recipe.isOn(routes, sceneId)
                    FilterChip(
                        selected = on,
                        enabled = on || recipe.fits(routes, sceneId),
                        onClick = { onReactionsChange(recipe.toggle(routes, sceneId)) },
                        label = { Text(recipe.title, style = MaterialTheme.typography.labelSmall) },
                    )
                }
            }
            if (shown.any { !it.isOn(routes, sceneId) && !it.fits(routes, sceneId) }) {
                Text(
                    "${routes.size} of ${ReactionRoute.MAX_ROUTES} reaction slots used, so the greyed-out " +
                        "reactions don't fit. Remove a route below to make room.",
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
        // The whole matrix: presets by feel, then every sound against every parameter (ui/ReactionGrid).
        val lanes = LocalMusicLanes.current
        if (lanes != null && !searching) {
            SectionHeader("Reaction presets")
            ControlHint("Each one layers several reactions on different parts of the music. It replaces the routes below.")
            FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(2.dp)) {
                ReactionPreset.entries.forEach { preset ->
                    FilterChip(
                        selected = false,
                        onClick = { lanes.edit { preset.routes } },
                        label = { Text(preset.title, style = MaterialTheme.typography.labelSmall) },
                    )
                }
            }
            SectionHeader("Every sound, every parameter")
            ControlHint(
                "Down the side, everything the engine hears; across, one group of parameters. Tap a cell to " +
                    "make that sound move it (tap again for stronger, then inverted, then off); hold it for details.",
            )
            ReactionGrid(lanes)
        }
        // The same routes as a list (re-roll, back to the style's DNA, and every route spelled out).
        var asList by rememberSaveable { mutableStateOf(lanes == null) }
        if (lanes != null) {
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                if (!asList) {
                    CrystalButton(compact = true, filled = false, onClick = onRerollReactions) { Text("Re-roll DNA") }
                    if (reactions != null) {
                        CrystalButton(compact = true, filled = false, onClick = { onReactionsChange(null) }) { Text("Back to style DNA") }
                    }
                }
                TextButton(onClick = { asList = !asList }) { Text(if (asList) "Hide the list" else "As a list") }
            }
        }
        if (asList || searching) {
            ReactionsSection(p, onChange, reactions, onReactionsChange, onRerollReactions)
        }
    }
}
