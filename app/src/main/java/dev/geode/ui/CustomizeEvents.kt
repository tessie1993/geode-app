package dev.geode.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import dev.geode.R
import dev.geode.pro.FreeTier
import dev.geode.render.EventRecipe
import dev.geode.render.ReactionRoute
import dev.geode.render.scene.BuildStyleReactionsUseCase
import dev.geode.render.scene.ParamKeys
import dev.geode.render.scene.ParamScope
import dev.geode.render.scene.ParamUnit
import dev.geode.render.scene.SceneParams

/**
 * Customize › Music: moments → actions. When this happens in the music, the picture does that.
 *
 * One row per [MusicMoment] (Kick, Snare … Loudness), each with its [MusicAction] chips (Pulse,
 * Flash, Zoom …) and one Strength. Every row is a view of the one reaction matrix, so nothing here
 * adds state: Auto is the style's DNA, and the first tap on a row makes the matrix the user's own,
 * with every other row still a copy of the DNA and so still marked Auto. A row the user changed
 * reads "Mine" until its ⋮ › Reset to Auto puts the DNA's routes for it back ([MusicMoments]).
 *
 * The ready-made [EventRecipe]s stay below as one-tap bundles across moments, then how strongly the
 * music moves things overall. Each route on its own is in Modulate › Reactions, so a listener who
 * only wants "the kick pulses" never meets attack and release.
 *
 * Free and Pro: the chips are the page's free control ([FreeTier.FREE_EVENTS_CONTROL]); each
 * moment's Strength and the overall one are try-then-pay, like every other Pro slider.
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
            val dna = remember(sceneId, p.reactSeed) { BuildStyleReactionsUseCase()(sceneId, p.reactSeed) }
            val routes = reactions ?: dna
            val actions = remember(sceneId) { MusicAction.entries.filter { it.target.scope.appliesTo(sceneId) } }
            SectionHeader(stringResource(R.string.music_moment_header))
            ControlHint(stringResource(R.string.music_moment_hint))
            // A moment's Strength is not a parameter a wave or the sound could drive, so no "react to music" here.
            CompositionLocalProvider(LocalReactTo provides null) {
                MusicMoment.entries.forEach { moment ->
                    MomentRow(moment, actions, routes, reactions, dna, onReactionsChange)
                }
            }
            // One list for the chips and the room note, so the note only ever speaks of chips on screen.
            val shown = remember(sceneId) { EventRecipe.entries.filter { it.routesFor(sceneId).isNotEmpty() } }
            val onCount = shown.count { it.isOn(routes, sceneId) }
            SectionHeader(stringResource(R.string.music_moment_recipes))
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
            val recipeFull = shown.any { !it.isOn(routes, sceneId) && !it.fits(routes, sceneId) }
            val momentFull =
                MusicMoment.entries.any { m ->
                    actions.any { a -> !MusicMoments.isOn(m, a, routes) && !MusicMoments.fits(m, a, routes) }
                }
            if (recipeFull || momentFull) {
                ControlHint(stringResource(R.string.music_moment_full, routes.size, ReactionRoute.MAX_ROUTES))
            }
        }
        SectionHeader(stringResource(R.string.music_moment_overall))
        LabeledSlider(ParamKeys.REACT_AMOUNT, p.reactAmount, 0f..2f, default = SceneParams.DEFAULT.reactAmount) {
            onChange(p.copy(reactAmount = it))
        }
        ControlHint("Each reaction, one by one, is in Modulate › Reactions.")
    }
}

/**
 * One moment: its name with Auto or Mine and a ⋮, its action chips, then its Strength while it
 * does anything. [routes] is what plays now (the user's list, else the DNA); [reactions] is the
 * user's list alone, which decides Mine.
 */
@Composable
private fun MomentRow(
    moment: MusicMoment,
    actions: List<MusicAction>,
    routes: List<ReactionRoute>,
    reactions: List<ReactionRoute>?,
    dna: List<ReactionRoute>,
    onReactionsChange: (List<ReactionRoute>?) -> Unit,
) {
    val name = stringResource(moment.label)
    val mine = MusicMoments.isMine(moment, reactions, dna)
    val onNames = actions.filter { MusicMoments.isOn(moment, it, routes) }.map { stringResource(it.label) }
    val canEdit = LocalProLock.current.allows(FreeTier.FREE_EVENTS_CONTROL)
    Column(Modifier.padding(top = 4.dp)) {
        // 48 dp, so the row keeps its height whether or not the ⋮ is there.
        Row(
            modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            // A heading, so TalkBack's headings control steps from moment to moment.
            Text(name, modifier = Modifier.semantics { heading() }, style = MaterialTheme.typography.bodyMedium)
            MomentMark(mine)
            Spacer(Modifier.weight(1f))
            // Like the slider rows' ⋮, it is drawn only when it has something to offer: an Auto moment has nothing to reset.
            if (canEdit && mine) {
                MomentMenu(name) { onReactionsChange(MusicMoments.resetToAuto(moment, reactions, dna)) }
            }
        }
        val nothing = stringResource(R.string.music_moment_nothing)
        ProGated(FreeTier.FREE_EVENTS_CONTROL, name, onNames.joinToString().ifEmpty { nothing }) {
            FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(2.dp)) {
                actions.forEach { action ->
                    val on = MusicMoments.isOn(moment, action, routes)
                    FilterChip(
                        selected = on,
                        enabled = on || MusicMoments.fits(moment, action, routes),
                        onClick = { onReactionsChange(MusicMoments.toggle(moment, action, routes, dna)) },
                        label = { Text(stringResource(action.label), style = MaterialTheme.typography.labelLarge) },
                    )
                }
            }
        }
        if (MusicMoments.routesOf(moment, routes).isNotEmpty()) {
            LabeledSlider(
                label = "Music ${moment.name.lowercase()} strength",
                value = MusicMoments.strength(moment, routes),
                range = 0f..MusicMoments.MAX_STRENGTH,
                display = stringResource(R.string.music_moment_strength, name),
                scope = ParamScope.UNIVERSAL,
                unit = ParamUnit.TIMES,
            ) { v -> onReactionsChange(MusicMoments.withStrength(moment, v, routes, dna)) }
        }
    }
}

/** "Mine" in the accent when the user set this moment, else a quiet "Auto". */
@Composable
private fun MomentMark(mine: Boolean) {
    if (mine) {
        val description = stringResource(R.string.music_moment_mine_description)
        Text(
            stringResource(R.string.music_moment_mine),
            modifier = Modifier.padding(start = 8.dp).semantics { contentDescription = description },
            style = MaterialTheme.typography.labelSmall,
            color = accentTextColor(),
        )
    } else {
        Text(
            stringResource(R.string.music_moment_auto),
            modifier = Modifier.padding(start = 8.dp),
            style = MaterialTheme.typography.labelSmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

/** A moment's ⋮ while the moment is the user's: Reset to Auto. Full 48 dp target. */
@Composable
private fun MomentMenu(
    name: String,
    onReset: () -> Unit,
) {
    var open by remember { mutableStateOf(false) }
    Box {
        IconButton(onClick = { open = true }) {
            Icon(
                Icons.Filled.MoreVert,
                contentDescription = stringResource(R.string.music_moment_more, name),
                tint = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
        DropdownMenu(expanded = open, onDismissRequest = { open = false }) {
            DropdownMenuItem(
                text = { Text(stringResource(R.string.music_moment_reset)) },
                onClick = {
                    open = false
                    onReset()
                },
            )
        }
    }
}
