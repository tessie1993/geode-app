package dev.geode.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.unit.dp
import dev.geode.pro.FreeTier
import dev.geode.render.EventRecipe
import dev.geode.render.ReactionRoute
import dev.geode.render.scene.BuildStyleReactionsUseCase
import dev.geode.render.scene.ParamKeys
import dev.geode.render.scene.SceneParams

/**
 * Customize › Music: what happens when something happens in the music.
 *
 * The simple layer of the reaction matrix: ready-made [EventRecipe]s as one-tap chips (the page's
 * free control), and how strongly the music moves things overall. Each recipe is the same edit as
 * adding its routes by hand and is saved the same way; the routes themselves, one by one, are in
 * Modulate › Reactions, so a listener who only wants "kick punch" never meets attack and release.
 */
@Composable
internal fun EventsTab(
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
    reactions: List<ReactionRoute>?,
    onReactionsChange: (List<ReactionRoute>?) -> Unit,
) {
    val sceneId = LocalSceneId.current
    val searching = LocalParamQuery.current.isNotBlank()
    Column {
        if (!searching) {
            ControlHint("When something happens in the music, something moves. Tap a reaction to add it; tap it again to take it off.")
            val dna = remember(sceneId, p.reactSeed) { BuildStyleReactionsUseCase()(sceneId, p.reactSeed) }
            val routes = reactions ?: dna
            // One list for the chips and the room note, so the note only ever speaks of chips on screen.
            val shown = remember(sceneId) { EventRecipe.entries.filter { it.routesFor(sceneId).isNotEmpty() } }
            val onCount = shown.count { it.isOn(routes, sceneId) }
            ProGated(FreeTier.FREE_EVENTS_CONTROL, "Reactions", "$onCount on") {
                FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(2.dp)) {
                    shown.forEach { recipe ->
                        val on = recipe.isOn(routes, sceneId)
                        FilterChip(
                            selected = on,
                            enabled = on || recipe.fits(routes, sceneId),
                            onClick = { onReactionsChange(recipe.toggle(routes, sceneId)) },
                            label = { Text(recipe.title, style = MaterialTheme.typography.labelLarge) },
                        )
                    }
                }
            }
            if (shown.any { !it.isOn(routes, sceneId) && !it.fits(routes, sceneId) }) {
                ControlHint(
                    "${routes.size} of ${ReactionRoute.MAX_ROUTES} reactions in use, so the greyed-out ones don't fit. " +
                        "Remove one in Modulate › Reactions to make room.",
                )
            }
        }
        SectionHeader("Strength")
        LabeledSlider(ParamKeys.REACT_AMOUNT, p.reactAmount, 0f..2f, default = SceneParams.DEFAULT.reactAmount) {
            onChange(p.copy(reactAmount = it))
        }
        ControlHint("Each reaction, one by one, is in Advanced › Reactions.")
    }
}
