package dev.geode.ui

import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import dev.geode.render.LfoTarget
import dev.geode.render.ModCurve
import dev.geode.render.ReactMode
import dev.geode.render.ReactSource
import dev.geode.render.ReactSourceGroup
import dev.geode.render.ReactionRoute
import kotlin.math.abs

/**
 * The reaction matrix as a grid: every sound the engine hears down the side (all of them, in the
 * catalogue's sections), one group of visual parameters across the top at a time. A dot is a route;
 * its size is how hard that sound moves that parameter.
 *
 * A tap steps a cell through off, light, medium, strong and inverted, with the mode picked from the
 * sound (smartMode): a hit pulses, a level follows, a clock keeps turning, a big moment or a counted
 * hit jumps somewhere new. A long press opens the details. The grid edits the same routes the style
 * DNA, the Events recipes and every Customize row use, so it is saved, exported and drawn by the
 * wallpaper and the dream like the rest.
 */
@OptIn(ExperimentalFoundationApi::class)
@Composable
internal fun ReactionGrid(lanes: MusicLanes) {
    val groups =
        remember(lanes.catalogue) {
            lanes.catalogue.targets
                .map { it.group }
                .filter { it != "NONE" && it != "SLOTS" }
                .distinct()
        }
    var group by rememberSaveable { mutableStateOf(groups.firstOrNull() ?: "MOTION") }
    var detail by remember { mutableStateOf<Pair<ReactSource, LfoTarget>?>(null) }
    val sceneId = LocalSceneId.current
    val columns =
        lanes.catalogue.targets
            .filter { it.group == group }
            .mapNotNull { info ->
                LfoTarget.entries
                    .getOrNull(info.id)
                    ?.takeIf { it.scope.appliesTo(sceneId) }
                    ?.let { it to info.label }
            }
    Column {
        Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            groups.forEach { g ->
                FilterChip(
                    selected = g == group,
                    onClick = { group = g },
                    label = { Text(g.lowercase().replaceFirstChar { it.uppercase() }, style = MaterialTheme.typography.labelSmall) },
                )
            }
        }
        val scroll = rememberScrollState()
        Box(Modifier.fillMaxWidth().height(420.dp)) {
            LazyColumn {
                item {
                    Row(Modifier.horizontalScroll(scroll)) {
                        Box(Modifier.width(ROW_LABEL))
                        columns.forEach { (_, label) ->
                            Text(
                                label,
                                style = MaterialTheme.typography.labelSmall,
                                maxLines = 2,
                                modifier = Modifier.width(CELL).padding(2.dp),
                            )
                        }
                    }
                }
                lanes.catalogue.sources.groupBy { it.group }.forEach { (section, infos) ->
                    item(key = "h$section") {
                        Text(
                            section.lowercase().replaceFirstChar { it.uppercase() },
                            style = MaterialTheme.typography.labelLarge,
                            modifier = Modifier.padding(top = 8.dp),
                        )
                    }
                    items(infos, key = { "s${it.id}" }) { info ->
                        val source = ReactSource.entries.getOrNull(info.id) ?: return@items
                        Row(Modifier.horizontalScroll(scroll), verticalAlignment = Alignment.CenterVertically) {
                            Text(
                                info.label,
                                style = MaterialTheme.typography.labelSmall,
                                maxLines = 1,
                                modifier = Modifier.width(ROW_LABEL),
                            )
                            columns.forEach { (target, label) ->
                                val at = lanes.routes.indexOfFirst { it.source == source && it.target == target }
                                val route = lanes.routes.getOrNull(at)
                                Box(
                                    Modifier
                                        .size(CELL)
                                        .combinedClickable(
                                            onClick = { lanes.edit { stepCell(lanes.routes, at, source, target) } },
                                            onLongClick = { if (lanes.canEdit) detail = source to target else lanes.onLocked() },
                                        ).semantics {
                                            contentDescription =
                                                "${info.label} moves $label" + (route?.let { " by %.2f".format(it.amount) } ?: ": off")
                                        },
                                    contentAlignment = Alignment.Center,
                                ) {
                                    val amount = if (route?.enabled == true) route.amount else 0f
                                    val dot = (6f + 18f * (abs(amount) / ReactionRoute.MAX_AMOUNT).coerceIn(0f, 1f)).dp
                                    Box(
                                        Modifier
                                            .size(if (route == null) 4.dp else dot)
                                            .clip(CircleShape)
                                            .background(
                                                when {
                                                    route == null -> MaterialTheme.colorScheme.outlineVariant
                                                    amount < 0f -> MaterialTheme.colorScheme.tertiary
                                                    else -> MaterialTheme.colorScheme.primary
                                                },
                                            ),
                                    )
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    detail?.let { (source, target) ->
        CellDetail(lanes, source, target, onDismiss = { detail = null })
    }
}

private val ROW_LABEL = 112.dp
private val CELL = 44.dp

/** The mode a tap picks for a sound (ParamMatrix's smart route, mirrored here for the grid). */
internal fun smartMode(source: ReactSource): ReactMode =
    when {
        source.group == ReactSourceGroup.CLOCKS -> ReactMode.SPIN
        source == ReactSource.HARD_CUT || source in COUNTED -> ReactMode.JUMP
        else -> ReactMode.FOLLOW
    }

private val COUNTED = setOf(ReactSource.KICK_4, ReactSource.KICK_8, ReactSource.KICK_16, ReactSource.SNARE_4)

private val STEPS = listOf(0.35f, 0.7f, 1.2f, -0.6f)

/** Off → light → medium → strong → inverted → off. */
private fun stepCell(
    routes: List<ReactionRoute>,
    at: Int,
    source: ReactSource,
    target: LfoTarget,
): List<ReactionRoute> {
    val route = routes.getOrNull(at)
    if (route == null) {
        if (routes.size >= ReactionRoute.MAX_ROUTES) return routes
        val mode = smartMode(source)
        return routes +
            ReactionRoute(
                enabled = true,
                source = source,
                target = target,
                mode = mode,
                amount = STEPS[0],
                curve = ModCurve.LINEAR,
                attackSeconds = if (source.isEvent) 0.01f else 0.08f,
                releaseSeconds =
                    when {
                        mode == ReactMode.JUMP -> 0.6f
                        source.isEvent -> 0.3f
                        else -> 0.4f
                    },
            )
    }
    val next = STEPS.indexOfFirst { abs(it - route.amount) < 0.01f }.let { if (it < 0) 0 else it + 1 }
    return if (next >= STEPS.size) {
        routes.filterIndexed { j, _ -> j != at }
    } else {
        routes.toMutableList().also { it[at] = route.copy(enabled = true, amount = STEPS[next]) }
    }
}

@Composable
private fun CellDetail(
    lanes: MusicLanes,
    source: ReactSource,
    target: LfoTarget,
    onDismiss: () -> Unit,
) {
    val at = lanes.routes.indexOfFirst { it.source == source && it.target == target }
    val route = lanes.routes.getOrNull(at) ?: newLane(source, target, false, STEPS[0]).copy(mode = smartMode(source))
    val update: (ReactionRoute) -> Unit = { r ->
        lanes.edit {
            if (at >= 0) {
                lanes.routes.toMutableList().also { it[at] = r }
            } else if (lanes.routes.size < ReactionRoute.MAX_ROUTES) {
                lanes.routes + r
            } else {
                lanes.routes
            }
        }
    }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("${source.label} → ${target.label}") },
        text = {
            Column {
                Text("How", style = MaterialTheme.typography.labelLarge)
                Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                    ReactMode.entries.forEach { m ->
                        FilterChip(selected = route.mode == m, onClick = { update(route.copy(mode = m)) }, label = { Text(m.label) })
                    }
                }
                Text("How hard  %+.2f".format(route.amount), style = MaterialTheme.typography.labelLarge)
                Slider(
                    value = route.amount,
                    onValueChange = { update(route.copy(amount = it)) },
                    valueRange = ReactionRoute.MIN_AMOUNT..ReactionRoute.MAX_AMOUNT,
                )
                Text("Rise  %.2f s".format(route.attackSeconds), style = MaterialTheme.typography.labelLarge)
                Slider(value = route.attackSeconds, onValueChange = { update(route.copy(attackSeconds = it)) }, valueRange = 0f..1f)
                Text("Fall  %.2f s".format(route.releaseSeconds), style = MaterialTheme.typography.labelLarge)
                Slider(value = route.releaseSeconds, onValueChange = { update(route.copy(releaseSeconds = it)) }, valueRange = 0f..3f)
                Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                    ModCurve.entries.forEach { c ->
                        FilterChip(selected = route.curve == c, onClick = { update(route.copy(curve = c)) }, label = { Text(c.label) })
                    }
                }
            }
        },
        confirmButton = { TextButton(onClick = onDismiss) { Text("Done") } },
        dismissButton = {
            if (at >= 0) {
                TextButton(onClick = {
                    lanes.edit { lanes.routes.filterIndexed { j, _ -> j != at } }
                    onDismiss()
                }) { Text("Remove") }
            }
        },
    )
}
