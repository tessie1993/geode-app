package dev.geode.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.AssistChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.RangeSlider
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import dev.geode.engine.bridge.GeodeNative
import dev.geode.render.LfoTarget
import dev.geode.render.ParamMatrixCatalog
import dev.geode.render.ReactMode
import dev.geode.render.ReactSource
import dev.geode.render.ReactionRoute
import kotlin.math.abs

/**
 * Customize's music lanes: every parameter is played by the music, and its row says how.
 *
 * A lane is the parameter's first Follow or Drive route (the style's DNA until the user edits one,
 * then the user's own list). A row shows it as a range: where the parameter rests (the scene value,
 * the left handle) and how far its sound pushes it (the route's amount, the right handle), with the
 * two ends named by the sound itself ("low note … high note", "between kicks … on the kick") and a
 * chip to choose another sound. A motion the music drives (rotation, speed, fly ...) has no resting
 * handle: it stands still without its sound, and its slider is how fast it runs at typical music.
 * Trigger targets (a cut, a spawn, a freeze) are a chip alone: what makes it happen.
 *
 * Editing the music side of a lane is Pro ([canEdit]); the resting value stays free like any slider.
 */
internal data class MusicLanes(
    val routes: List<ReactionRoute>,
    val catalogue: ParamMatrixCatalog,
    val canEdit: Boolean,
    val onRoutesChange: (List<ReactionRoute>) -> Unit,
    val onLocked: () -> Unit,
) {
    // Step and Toggle routes are accents (a hue jump per note), not the lane that plays the parameter.
    fun laneIndex(t: LfoTarget): Int =
        routes.indexOfFirst { it.enabled && it.target == t && it.mode != ReactMode.STEP && it.mode != ReactMode.TOGGLE }

    /** [routes] with the lane for [t] replaced by [route], or added; null removes it. */
    fun withLane(
        t: LfoTarget,
        route: ReactionRoute?,
    ): List<ReactionRoute> {
        val i = laneIndex(t)
        return when {
            i >= 0 && route != null -> routes.toMutableList().also { it[i] = route }
            i >= 0 -> routes.filterIndexed { j, _ -> j != i }
            route != null && routes.size < ReactionRoute.MAX_ROUTES -> routes + route
            else -> routes
        }
    }

    fun edit(change: () -> List<ReactionRoute>) {
        if (canEdit) onRoutesChange(change()) else onLocked()
    }
}

internal val LocalMusicLanes = staticCompositionLocalOf<MusicLanes?> { null }

/** The native parameter matrix, read once per process; null where the library cannot answer (previews). */
internal fun musicCatalogue(): ParamMatrixCatalog? = catalogueOnce

private val catalogueOnce: ParamMatrixCatalog? by lazy {
    runCatching { ParamMatrixCatalog.parse(GeodeNative.paramMatrixCatalog()) }
        .getOrNull()
        ?.takeIf { it.targets.isNotEmpty() }
}

/**
 * The row for one parameter played by the music. [value] and [onValue] are the scene parameter (the
 * resting value, or for a motion how fast it runs); the lane comes from [LocalMusicLanes].
 */
@Composable
internal fun MusicRangeRow(
    lanes: MusicLanes,
    target: LfoTarget,
    display: String,
    value: Float,
    range: ClosedFloatingPointRange<Float>,
    onValue: (Float) -> Unit,
    trailing: @Composable () -> Unit = {},
) {
    val info = lanes.catalogue.target(target)
    val laneAt = lanes.laneIndex(target)
    val lane = lanes.routes.getOrNull(laneAt)
    val drive = info?.defaultDrive != null
    val source =
        lane?.source ?: info?.defaultDrive?.let { name -> ReactSource.entries.firstOrNull { it.name == name } }
    val poles = source?.let { lanes.catalogue.source(it) }
    val scale = info?.scale?.takeIf { it > 0f } ?: 1f
    var picking by remember { mutableStateOf(false) }
    Column(Modifier.padding(vertical = 2.dp)) {
        Row(modifier = Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Text(display, style = MaterialTheme.typography.labelSmall, modifier = Modifier.weight(1f))
            AssistChip(
                onClick = { if (lanes.canEdit) picking = true else lanes.onLocked() },
                label = { Text("♪ ${source?.label ?: "No sound"}", style = MaterialTheme.typography.labelSmall) },
                modifier = Modifier.semantics { contentDescription = "Sound that moves $display" },
            )
            trailing()
        }
        if (drive) {
            // A motion: still without its sound; the slider is its pace at typical music.
            CrystalSlider(
                value = value,
                onValueChange = onValue,
                valueRange = range,
                modifier =
                    Modifier.fillMaxWidth().semantics {
                        contentDescription = display
                        stateDescription = "%.2f at typical music".format(value)
                    },
            )
        } else {
            val amount = lane?.amount ?: 0f
            val loud = (value + amount * scale).coerceIn(range.start, range.endInclusive)
            val up = amount >= 0f
            RangeSlider(
                value = if (up) value..loud else loud..value,
                onValueChange = { r ->
                    val rest = if (up) r.start else r.endInclusive
                    val push = if (up) r.endInclusive else r.start
                    if (abs(rest - value) > 1e-4f) onValue(rest)
                    if (abs(push - loud) > 1e-4f && source != null) {
                        val newAmount = (push - rest) / scale
                        lanes.edit {
                            lanes.withLane(target, (lane ?: lanes.catalogue.cell(source, target, 0f)).copy(amount = newAmount))
                        }
                    }
                },
                valueRange = range,
                modifier =
                    Modifier.fillMaxWidth().semantics {
                        contentDescription = display
                        stateDescription = "rests at %.2f, %s %.2f".format(value, poles?.high ?: "music", loud)
                    },
            )
        }
        if (poles != null) {
            Row(Modifier.fillMaxWidth()) {
                val left = if (drive || (lane?.amount ?: 0f) >= 0f) poles.low else poles.high
                val right = if (drive || (lane?.amount ?: 0f) >= 0f) poles.high else poles.low
                Text(left, style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                Text(
                    right,
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.weight(1f).padding(start = 8.dp),
                    textAlign = TextAlign.End,
                )
            }
        }
    }
    if (picking) {
        SoundPicker(
            lanes = lanes,
            current = source,
            allowNone = !drive,
            onPick = { s ->
                picking = false
                lanes.edit {
                    when {
                        s == null -> lanes.withLane(target, null)
                        lane != null -> lanes.withLane(target, lane.copy(source = s))
                        else -> lanes.withLane(target, lanes.catalogue.cell(s, target, if (drive) 1f else 0.3f, drive))
                    }
                }
            },
            onDismiss = { picking = false },
        )
    }
}

/** A trigger target (a camera cut, a spawn, a freeze, a shatter): the sound that makes it happen. */
@Composable
internal fun MusicTriggerRow(
    lanes: MusicLanes,
    target: LfoTarget,
    display: String,
    hint: String,
) {
    val laneAt = lanes.laneIndex(target)
    val lane = lanes.routes.getOrNull(laneAt)
    var picking by remember { mutableStateOf(false) }
    Row(modifier = Modifier.fillMaxWidth().padding(vertical = 2.dp), verticalAlignment = Alignment.CenterVertically) {
        Column(Modifier.weight(1f)) {
            Text(display, style = MaterialTheme.typography.labelSmall)
            Text(hint, style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
        AssistChip(
            onClick = { if (lanes.canEdit) picking = true else lanes.onLocked() },
            label = { Text(if (lane != null) "on ${lane.source.label}" else "Never", style = MaterialTheme.typography.labelSmall) },
            modifier = Modifier.semantics { contentDescription = "What makes $display happen" },
        )
    }
    if (picking) {
        SoundPicker(
            lanes = lanes,
            current = lane?.source,
            allowNone = true,
            onPick = { s ->
                picking = false
                lanes.edit {
                    when {
                        s == null -> lanes.withLane(target, null)
                        lane != null -> lanes.withLane(target, lane.copy(source = s))
                        else -> lanes.withLane(target, lanes.catalogue.cell(s, target, 1f))
                    }
                }
            },
            onDismiss = { picking = false },
        )
    }
}

/** Every sound the engine hears, in the catalogue's sections, each with what its two ends mean. */
@Composable
internal fun SoundPicker(
    lanes: MusicLanes,
    current: ReactSource?,
    allowNone: Boolean,
    onPick: (ReactSource?) -> Unit,
    onDismiss: () -> Unit,
) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Which sound moves it") },
        text = {
            Column(modifier = Modifier.height(400.dp).verticalScroll(rememberScrollState())) {
                if (allowNone) TextButton(onClick = { onPick(null) }) { Text(if (current == null) "▶ No sound" else "No sound") }
                lanes.catalogue.sources.groupBy { it.group }.forEach { (group, infos) ->
                    Text(
                        group.lowercase().replaceFirstChar { it.uppercase() },
                        style = MaterialTheme.typography.labelLarge,
                        modifier = Modifier.padding(top = 8.dp, start = 12.dp),
                    )
                    infos.forEach { info ->
                        val s = ReactSource.entries.getOrNull(info.id) ?: return@forEach
                        TextButton(onClick = { onPick(s) }) {
                            Text((if (s == current) "▶ " else "") + "${info.label}  ·  ${info.low} … ${info.high}")
                        }
                    }
                }
            }
        },
        confirmButton = { TextButton(onClick = onDismiss) { Text("Close") } },
    )
}
