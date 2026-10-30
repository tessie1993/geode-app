package dev.geode.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import dev.geode.analysis.IntelligenceMode
import dev.geode.render.EventRecipe
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
    intelligenceMode: IntelligenceMode,
    onIntelligenceModeChange: (IntelligenceMode) -> Unit,
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
        ReactionsSection(p, onChange, reactions, onReactionsChange, onRerollReactions)
        if (!searching) {
            Text(
                "When the song changes section",
                modifier = Modifier.padding(top = 14.dp),
                style = MaterialTheme.typography.titleSmall,
            )
            Text(
                "Manual keeps your style. Suggest proposes one that suits the track; Auto picks a new " +
                    "one for each section as the song plays.",
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            CrystalSegmented(
                options = IntelligenceMode.entries.map { it.name.lowercase().replaceFirstChar(Char::uppercase) },
                selected = IntelligenceMode.entries.indexOf(intelligenceMode),
                onSelect = { onIntelligenceModeChange(IntelligenceMode.entries[it]) },
            )
        }
    }
}
