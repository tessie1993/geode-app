package dev.geode.ui

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ExpandMore
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material3.Checkbox
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.unit.dp
import dev.geode.R
import dev.geode.render.EventRecipe
import dev.geode.render.HandReactions
import dev.geode.render.ReactionRoute
import dev.geode.render.scene.BuildStyleReactionsUseCase
import dev.geode.render.scene.ParamScope
import dev.geode.render.scene.ParamUnit
import dev.geode.render.scene.SceneParams
import dev.geode.ui.theme.GeodeSpacing
import dev.geode.ui.theme.LocalGeodeMotion

/**
 * Customize › Music: moments → actions. When this happens in the music, the picture does that.
 *
 * One row per [MusicMoment] (Kick, Snare … Loudness): its name, what it does as one summary button
 * ("Auto · Pulse, Zoom", or "Pulse, Zoom" once it is the user's) that opens a checkable menu of the
 * [MusicAction]s in two groups (light, then motion), and its ⋮ (UI V2 rule 2: one row and a menu,
 * not seven chips per moment). A tap on the name opens the row in place to show its Strength, one
 * row at a time, so ten moments read as ten lines.
 *
 * The hand wins per moment: a tick, or a drag of the Strength, makes that moment the user's ("Mine",
 * kept in the hand mask beside their routes, [HandReactions]) with the actions it played copied in
 * (native's merge); every other row stays Auto and keeps following the live DNA as the style or seed
 * changes, and the director keeps off the moments marked Mine. ⋮ › Reset to Auto hands it back.
 * Native decides all of it ([MusicMoments] only calls it); this page draws the rows and sends taps.
 *
 * The ready-made [EventRecipe]s stay below as one menu of one-tap bundles across moments. How
 * strongly the music moves things overall is Music's Basics; each route on its own is Music ›
 * Reactions.
 *
 * Pro, like every section below a category's Basics: a free user can open it and see the moments,
 * and a tap on a menu or a Strength opens the paywall ([MOMENTS_PIECE] is the piece's lock key).
 */
@Composable
internal fun EventsTab(
    p: SceneParams,
    hand: HandReactions,
    onReactionsChange: (List<ReactionRoute>?) -> Unit,
    onHandChange: (HandReactions) -> Unit,
) {
    val sceneId = LocalSceneId.current
    val searching = LocalParamQuery.current.isNotBlank()
    Column {
        if (!searching) {
            val seed = p.reactSeed
            // What plays now (the native merge of the user's routes and the live DNA) and each row's state.
            val routes = remember(sceneId, seed, hand) { BuildStyleReactionsUseCase().playing(sceneId, seed, hand.routes, hand.mask) }
            val rows = remember(sceneId, seed, hand) { MusicMoments.rows(sceneId, seed, hand) }
            val actions = remember(sceneId) { MusicMoments.actionsFor(sceneId) }
            var open by rememberSaveable { mutableIntStateOf(-1) }
            val reactTo = LocalReactTo.current
            SectionHeader(stringResource(R.string.music_moment_header))
            ControlHint(stringResource(R.string.music_moment_hint))
            // A moment's Strength is not a parameter a wave or the sound could drive, so its slider has no
            // "react to music"; the row's own ⋮ offers the reaction editor instead.
            CompositionLocalProvider(LocalReactTo provides null) {
                MusicMoment.entries.forEach { moment ->
                    MomentRow(
                        moment = moment,
                        row = rows[moment.ordinal],
                        actions = actions,
                        expanded = open == moment.ordinal,
                        onExpand = { open = if (open == moment.ordinal) -1 else moment.ordinal },
                        reactTo = reactTo,
                    ) { edit -> onHandChange(edit(sceneId, seed, hand)) }
                }
            }
            // One list for the menu and the room note, so the note only ever speaks of recipes on offer.
            val shown = remember(sceneId) { EventRecipe.entries.filter { it.routesFor(sceneId).isNotEmpty() } }
            val onCount = shown.count { it.isOn(routes, sceneId) }
            val recipesName = stringResource(R.string.music_moment_recipes)
            val recipesSummary = stringResource(R.string.music_recipes_summary, onCount)
            Row(Modifier.fillMaxWidth().heightIn(min = GeodeSpacing.TouchTarget), verticalAlignment = Alignment.CenterVertically) {
                Text(recipesName, style = MaterialTheme.typography.bodyMedium, modifier = Modifier.weight(1f))
                ProGated(MOMENTS_PIECE, "Reactions", recipesSummary) {
                    MultiPickButton(
                        name = stringResource(R.string.music_recipes_button),
                        summary = recipesSummary,
                        picks =
                            shown.map { r ->
                                val on = r.isOn(routes, sceneId)
                                MenuPick(r.title, checked = on, enabled = on || r.fits(routes, sceneId))
                            },
                    ) { i -> onReactionsChange(shown[i].toggle(routes, sceneId)) }
                }
            }
            val recipeFull = shown.any { !it.isOn(routes, sceneId) && !it.fits(routes, sceneId) }
            val momentFull = rows.any { row -> actions.any { a -> a !in row.on && a !in row.fits } }
            if (recipeFull || momentFull) {
                ControlHint(stringResource(R.string.music_moment_full, routes.size, ReactionRoute.MAX_ROUTES))
            }
        }
    }
}

/** One tap on the page, sent to native with the style, the seed and the user's side. */
private typealias MomentEdit = (sceneId: String, seed: Int, hand: HandReactions) -> HandReactions

/** The light actions first, then the ones that move the picture; a divider sits between the groups. */
private val LIGHT_ACTIONS = listOf(MusicAction.PULSE, MusicAction.FLASH, MusicAction.COLOUR)
private val MOTION_ACTIONS = listOf(MusicAction.ZOOM, MusicAction.SHAKE, MusicAction.CAMERA, MusicAction.WARP)

/**
 * One moment as one row: its name (with "Mine" once it is the user's and a chevron while it has a
 * Strength to show), the actions summary that opens the checkable menu, and its ⋮. [row] is native's
 * reading of it ([MusicMoments.rows]); [onEdit] sends a tap. While [expanded], the Strength slider
 * opens under the row.
 */
@Composable
private fun MomentRow(
    moment: MusicMoment,
    row: MomentState,
    actions: List<MusicAction>,
    expanded: Boolean,
    onExpand: () -> Unit,
    reactTo: (() -> Unit)?,
    onEdit: (MomentEdit) -> Unit,
) {
    val name = stringResource(moment.label)
    val mine = row.mine
    val ordered = (LIGHT_ACTIONS + MOTION_ACTIONS).filter { it in actions }
    val onNames = ordered.filter { it in row.on }.map { stringResource(it.label) }
    val playing = onNames.joinToString().ifEmpty { stringResource(R.string.music_moment_nothing) }
    // An Auto row still says what it plays now, so the user sees what a tick would start from.
    val summary = if (mine) playing else stringResource(R.string.music_moment_auto_summary, playing)
    val hasStrength = row.routes > 0
    val motion = LocalGeodeMotion.current
    val toggleLabel = stringResource(if (expanded) R.string.music_moment_hide_strength else R.string.music_moment_show_strength)
    val expandedState = stringResource(if (expanded) R.string.customize_card_expanded else R.string.customize_card_collapsed)
    val turn by animateFloatAsState(if (expanded && hasStrength) HALF_TURN else 0f, motion.fastSpatialSpec(), label = "moment chevron")
    Column {
        Row(Modifier.fillMaxWidth().heightIn(min = MOMENT_ROW_HEIGHT), verticalAlignment = Alignment.CenterVertically) {
            Row(
                Modifier
                    .weight(1f)
                    .heightIn(min = GeodeSpacing.TouchTarget)
                    .clickable(
                        enabled = hasStrength,
                        role = Role.Button,
                        onClickLabel = toggleLabel,
                        onClick = onExpand,
                    )
                    // A heading, so TalkBack's headings control steps from moment to moment.
                    .semantics(mergeDescendants = true) {
                        heading()
                        if (hasStrength) stateDescription = expandedState
                    },
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text(name, style = MaterialTheme.typography.bodyMedium)
                if (mine) MineMark()
                if (hasStrength) {
                    Icon(
                        Icons.Filled.ExpandMore,
                        contentDescription = null,
                        modifier = Modifier.padding(start = GeodeSpacing.XS).graphicsLayer { rotationZ = turn },
                        tint = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
            }
            ProGated(MOMENTS_PIECE, name, summary) {
                val none = stringResource(R.string.music_moment_none)
                val picks =
                    ordered.map { a -> MenuPick(stringResource(a.label), a in row.on, a in row.on || a in row.fits) } +
                        MenuPick(none, row.on.isEmpty(), row.on.isNotEmpty())
                val motionAt = ordered.indexOfFirst { it in MOTION_ACTIONS }
                MultiPickButton(
                    name = stringResource(R.string.music_moment_actions, name),
                    summary = summary,
                    picks = picks,
                    dividersBefore = setOfNotNull(motionAt.takeIf { it > 0 }, ordered.size),
                ) { i ->
                    if (i == ordered.size) {
                        // None: every action this moment plays, off, one native edit after another.
                        val off = ordered.filter { it in row.on }
                        onEdit { s, seed, hand -> off.fold(hand) { h, a -> MusicMoments.toggle(s, seed, h, moment, a) } }
                    } else {
                        onEdit { s, seed, hand -> MusicMoments.toggle(s, seed, hand, moment, ordered[i]) }
                    }
                }
            }
            MomentMenu(name, row, reactTo, onEdit = onEdit, moment = moment)
        }
        AnimatedVisibility(
            visible = expanded && hasStrength,
            enter = expandVertically(motion.defaultSpatialSpec()) + fadeIn(motion.fastEffectsSpec()),
            exit = shrinkVertically(motion.fastSpatialSpec()) + fadeOut(motion.fastEffectsSpec()),
        ) {
            // Dragging an Auto row's Strength makes it Mine with the actions it played (native copies them in).
            LabeledSlider(
                label = "Music ${moment.name.lowercase()} strength",
                value = row.strength,
                range = 0f..MusicMoments.MAX_STRENGTH,
                display = stringResource(R.string.music_moment_strength, name),
                scope = ParamScope.UNIVERSAL,
                unit = ParamUnit.TIMES,
            ) { v -> onEdit { s, seed, hand -> MusicMoments.withStrength(s, seed, hand, moment, v) } }
        }
    }
}

private const val HALF_TURN = 180f
private val MOMENT_ROW_HEIGHT = 56.dp

/** "Mine" in the accent when the user set this moment. An Auto row says Auto in its summary instead. */
@Composable
private fun MineMark() {
    val description = stringResource(R.string.music_moment_mine_description)
    Text(
        stringResource(R.string.music_moment_mine),
        modifier = Modifier.padding(start = GeodeSpacing.S).semantics { contentDescription = description },
        style = MaterialTheme.typography.labelSmall,
        color = accentTextColor(),
    )
}

/**
 * A moment's ⋮: What is this, Keep when randomizing, Make it react to music ♛ and, while the moment
 * is the user's, Reset to Auto. Full 48 dp target.
 *
 * Keep is the hand mask: a kept moment is one the user owns, which the style's reroll and the director
 * leave alone, so ticking it makes the moment Mine with what it plays now (a Strength edit at its
 * current Strength, which copies the playing routes in) and unticking it hands the moment back.
 */
@Composable
private fun MomentMenu(
    name: String,
    row: MomentState,
    reactTo: (() -> Unit)?,
    moment: MusicMoment,
    onEdit: (MomentEdit) -> Unit,
) {
    var open by remember { mutableStateOf(false) }
    var explaining by remember { mutableStateOf(false) }
    val lock = LocalProLock.current
    val canEdit = lock.allows(MOMENTS_PIECE)
    val edit: (MomentEdit) -> Unit = { e -> if (canEdit) onEdit(e) else lock.onLocked() }
    Box {
        IconButton(onClick = { open = true }) {
            Icon(
                Icons.Filled.MoreVert,
                contentDescription = stringResource(R.string.music_moment_more, name),
                tint = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
        DropdownMenu(expanded = open, onDismissRequest = { open = false }) {
            DropdownMenuItem(text = { Text(stringResource(R.string.customize_what_is_this)) }, onClick = {
                open = false
                explaining = true
            })
            DropdownMenuItem(
                text = { Text(stringResource(R.string.music_moment_keep)) },
                enabled = row.mine || row.routes > 0,
                trailingIcon = { Checkbox(checked = row.mine, onCheckedChange = null, enabled = row.mine || row.routes > 0) },
                onClick = {
                    open = false
                    if (row.mine) {
                        edit { s, seed, hand -> MusicMoments.resetToAuto(s, seed, hand, moment) }
                    } else {
                        edit { s, seed, hand -> MusicMoments.withStrength(s, seed, hand, moment, row.strength) }
                    }
                },
            )
            if (reactTo != null) {
                DropdownMenuItem(
                    text = { Text(stringResource(R.string.customize_make_it_react)) },
                    trailingIcon =
                        if (!lock.unlocked) {
                            { ProCrown() }
                        } else {
                            null
                        },
                    onClick = {
                        open = false
                        reactTo()
                    },
                )
            }
            if (row.mine) {
                DropdownMenuItem(text = { Text(stringResource(R.string.music_moment_reset)) }, onClick = {
                    open = false
                    edit { s, seed, hand -> MusicMoments.resetToAuto(s, seed, hand, moment) }
                })
            }
        }
    }
    if (explaining) AboutDialog(name, stringResource(R.string.music_moment_about, name)) { explaining = false }
}

/** Music › Moments as core/viz/CustomizeMap names it: the lock key of every Pro control on it. */
internal const val MOMENTS_PIECE = "@moments"
