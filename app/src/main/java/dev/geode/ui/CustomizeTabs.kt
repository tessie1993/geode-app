package dev.geode.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.selection.toggleable
import androidx.compose.foundation.systemGestureExclusion
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowDropDown
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material.icons.filled.PushPin
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Checkbox
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.compositionLocalOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.CustomAccessibilityAction
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.customActions
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.semantics.toggleableState
import androidx.compose.ui.state.ToggleableState
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import dev.geode.R
import dev.geode.engine.bridge.GeodeNative
import dev.geode.render.scene.ParamFormat
import dev.geode.render.scene.ParamKeys
import dev.geode.render.scene.ParamScope
import dev.geode.render.scene.ParamUnit
import dev.geode.render.scene.SceneIds
import dev.geode.render.scene.SceneParams
import dev.geode.ui.theme.GeodeSpacing
import kotlin.math.abs

val LocalParamLocks =
    compositionLocalOf<Pair<Set<String>, (String) -> Unit>> { emptySet<String>() to {} }

/**
 * The style the panel is editing.
 *
 * Every control asks [ParamScope] whether it can move THIS style and draws nothing if it cannot
 * (R4: hidden, never greyed out and lying). Keeping it in a composition local rather than in each
 * tab's signature is what makes that automatic — a new control is gated the moment it is given a
 * [ParamKeys] name, instead of when somebody remembers to thread another boolean through.
 */
val LocalSceneId = compositionLocalOf { SceneIds.DEFAULT }

/** The panel-wide parameter search. Blank shows the tab as authored. */
val LocalParamQuery = compositionLocalOf { "" }

/**
 * Whether the panel shows its explanations. Off by default (⋯ › Show tips): the owner asked for
 * the controls first and no hint paragraphs between them, and the names already say most of it.
 */
val LocalShowTips = compositionLocalOf { false }

/** Opens the reaction editor ("Make it react to music"); null where the panel has none to open. */
internal val LocalReactTo = compositionLocalOf<(() -> Unit)?> { null }

/** How far a Pro control the user may not keep is dimmed. No lock icon per row: the ♛ is on the page. */
private const val LOCKED_ALPHA = 0.6f

/**
 * Which Customize controls this user may change.
 *
 * WHY a composition local, like [LocalSceneId]: the free tier is "one headline control per simple
 * page" (dev.geode.pro.FreeTier), and every slider, switch and chip row already knows its own
 * [ParamKeys] name, so each control can ask for itself. A free user still SEES a locked control
 * and its value (the point is to show what Pro adds), and a tap anywhere on it opens the paywall.
 * The default is unlocked, so screen tests and previews need no provider.
 */
@Immutable
internal class ProLock(
    val unlocked: Boolean,
    val free: Set<String>,
    val onLocked: () -> Unit,
) {
    fun allows(key: String): Boolean = unlocked || key in free

    companion object {
        val OPEN = ProLock(unlocked = true, free = emptySet(), onLocked = {})
    }
}

internal val LocalProLock = compositionLocalOf { ProLock.OPEN }

/** True inside a [ProGated] block that is locked: controls draw the Pro mark instead of their own buttons. */
private val LocalLockedByPro = compositionLocalOf { false }

/**
 * Draws [content] as usual when [key] is allowed. Otherwise draws it read-only under a layer that
 * takes every touch and opens the paywall, and tells TalkBack one thing: the control's name, its
 * value, and that it is part of Pro.
 */
@Composable
internal fun ProGated(
    key: String,
    label: String = key,
    value: String? = null,
    content: @Composable () -> Unit,
) {
    val lock = LocalProLock.current
    if (lock.allows(key)) {
        content()
        return
    }
    val unlockLabel = stringResource(R.string.customize_a11y_unlock_pro)
    val lockedLabel = stringResource(R.string.customize_a11y_part_of_pro, label)
    Box {
        CompositionLocalProvider(LocalLockedByPro provides true) {
            // A Column, so a block of several rows (a whole Advanced page in search) still stacks.
            Column(Modifier.alpha(LOCKED_ALPHA).clearAndSetSemantics {}) { content() }
        }
        Box(
            Modifier
                .matchParentSize()
                .clickable(onClickLabel = unlockLabel, role = Role.Button, onClick = lock.onLocked)
                .semantics {
                    contentDescription = lockedLabel
                    if (value != null) stateDescription = value
                },
        )
    }
}

/**
 * True when this control both applies to the active style and matches the search.
 *
 * [scope] defaults to the one the parameter's own name declares. It is only passed explicitly for
 * the handful of controls that are a master switch rather than a rolled parameter, and so have no
 * entry of their own in [ParamScope.of]. The search matches the shown name as well as the key, so
 * a control renamed on screen is found by the word the user sees.
 */
@Composable
internal fun visible(
    paramKey: String,
    scope: ParamScope,
    display: String = paramKey,
): Boolean {
    if (!scope.appliesTo(LocalSceneId.current)) return false
    val query = LocalParamQuery.current.trim()
    return query.isEmpty() || paramKey.contains(query, ignoreCase = true) || display.contains(query, ignoreCase = true)
}

/** True when a whole section can be drawn: it applies here, and no search is narrowing the panel. */
@Composable
internal fun sectionVisible(scope: ParamScope = ParamScope.UNIVERSAL): Boolean =
    LocalParamQuery.current.isBlank() && scope.appliesTo(LocalSceneId.current)

@Composable
internal fun SectionHeader(
    title: String,
    scope: ParamScope = ParamScope.UNIVERSAL,
) {
    if (!sectionVisible(scope)) return
    Text(
        title,
        modifier = Modifier.fillMaxWidth().padding(top = 16.dp, bottom = 4.dp).semantics { heading() },
        style = MaterialTheme.typography.titleSmall,
        color = accentTextColor(),
    )
}

@Composable
internal fun ControlHint(
    text: String,
    scope: ParamScope = ParamScope.UNIVERSAL,
) {
    if (!sectionVisible(scope) || !LocalShowTips.current) return
    Text(
        text,
        modifier = Modifier.padding(top = 4.dp, bottom = 2.dp),
        style = MaterialTheme.typography.bodySmall,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
    )
}

/**
 * A row's ⋮: reset, keep when randomizing, make it react to music. One small button per row in
 * place of the old Reset text button and lock toggle, so a glance down a page reads names and
 * values, not icons; the overflow is where Android users look for a row's actions.
 */
@Composable
private fun RowMenu(
    name: String,
    lockKey: String,
    onReset: (() -> Unit)?,
    about: String? = null,
) {
    val (kept, toggle) = LocalParamLocks.current
    // The engine says which controls a roll can move (core/viz/ParamRandomizer); only those can be kept.
    val lockable = remember(lockKey) { GeodeNative.customizeLockable(lockKey) }
    val reactTo = LocalReactTo.current
    val hasActions = lockable || onReset != null
    if (!hasActions && reactTo == null && about == null) return
    var open by remember { mutableStateOf(false) }
    var explaining by remember { mutableStateOf(false) }
    Box {
        // 40 dp drawn; IconButton still answers to the 48 dp target around it.
        IconButton(onClick = { open = true }, modifier = Modifier.size(40.dp)) {
            Icon(
                Icons.Filled.MoreVert,
                contentDescription = stringResource(R.string.customize_a11y_more_for, name),
                tint = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
        DropdownMenu(expanded = open, onDismissRequest = { open = false }) {
            if (about != null) {
                DropdownMenuItem(text = { Text(stringResource(R.string.customize_what_is_this)) }, onClick = {
                    open = false
                    explaining = true
                })
            }
            if (onReset != null) {
                DropdownMenuItem(text = { Text("Reset") }, onClick = {
                    open = false
                    onReset()
                })
            }
            if (lockable) {
                DropdownMenuItem(
                    text = { Text("Keep when randomizing") },
                    trailingIcon = { Checkbox(checked = lockKey in kept, onCheckedChange = null) },
                    onClick = {
                        open = false
                        toggle(lockKey)
                    },
                )
            }
            if (reactTo != null) {
                DropdownMenuItem(text = { Text("Make it react to music") }, onClick = {
                    open = false
                    reactTo()
                })
            }
        }
    }
    if (explaining && about != null) AboutDialog(name, about) { explaining = false }
}

/**
 * ⋮ › What is this: a control's explanation, kept out of the page (UI V2 rule 3: no caption
 * paragraphs between controls) and one tap away for whoever wants it.
 */
@Composable
internal fun AboutDialog(
    title: String,
    text: String,
    onDismiss: () -> Unit,
) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(title) },
        text = { Text(text, style = MaterialTheme.typography.bodyMedium) },
        confirmButton = { TextButton(onClick = onDismiss) { Text(stringResource(R.string.customize_about_close)) } },
    )
}

/** The small pin beside a name that Randomize leaves alone; the switch for it is in the row's ⋮. */
@Composable
private fun KeptMark(lockKey: String) {
    if (lockKey !in LocalParamLocks.current.first) return
    Icon(
        Icons.Filled.PushPin,
        contentDescription = stringResource(R.string.customize_a11y_kept),
        modifier = Modifier.padding(start = 4.dp).size(14.dp),
        tint = accentTextColor(),
    )
}

@Composable
internal fun ControlLabelRow(
    text: String,
    lockKey: String,
) {
    Row(
        modifier = Modifier.fillMaxWidth().heightIn(min = 40.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(text, style = MaterialTheme.typography.bodyMedium)
        KeptMark(lockKey)
        Spacer(Modifier.weight(1f))
        if (!LocalLockedByPro.current) RowMenu(text, lockKey, onReset = null)
    }
}

/**
 * A palette pick ([second] for the blend's second slot) as one dropdown row, the built-in palettes then
 * the user's own, with the gradient it paints under it. The chip wall it replaces is still
 * [PaletteSlotSelector] in the palette maker, which owns that page.
 */
@Composable
internal fun PaletteDropdown(
    lockKey: String,
    name: String,
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
    palettes: SavedPalettes,
    second: Boolean,
) {
    val saved = palettes.items
    val labels = SceneParams.PALETTES.map { it.first } + saved.map { it.name }
    Column {
        DropdownRow(
            name = name,
            options = labels,
            selectedIndex = paletteChipIndex(p, saved, second),
            lockKey = lockKey,
            placeholder = stringResource(R.string.look_summary_custom),
        ) { i -> onChange(paletteChipSelected(p, saved, i, second)) }
        GradientPreview(
            baseHue = if (second) p.palette2Base else p.paletteBase,
            hueSpan = if (second) p.palette2Range else p.paletteRange,
            modifier = Modifier.fillMaxWidth(),
        )
    }
}

/**
 * A pick from a list as one row: "Name   value ▾  ⋮" (UI V2 rule 2: dropdowns, not chip walls).
 *
 * A tap anywhere on the name or value opens the options as a menu with the current one checked, so
 * a page reads as names and values, and a long list (the camera paths, a fluid's patterns) costs
 * one line instead of a wall of chips. [lockKey] gives the row its kept mark and ⋮ (Keep when
 * randomizing); null for a pick that is not a rolled parameter (a reaction's mode, a layer's
 * blend). [placeholder] is shown when nothing in [options] is the current value.
 */
@Composable
internal fun DropdownRow(
    name: String,
    options: List<String>,
    selectedIndex: Int,
    lockKey: String? = null,
    about: String? = null,
    placeholder: String = "",
    onSelect: (Int) -> Unit,
) {
    val enabled = !LocalLockedByPro.current
    val value = options.getOrNull(selectedIndex) ?: placeholder
    var open by remember { mutableStateOf(false) }
    val chooseLabel = stringResource(R.string.customize_a11y_choose, name)
    Row(Modifier.fillMaxWidth().heightIn(min = GeodeSpacing.TouchTarget), verticalAlignment = Alignment.CenterVertically) {
        Box(Modifier.weight(1f)) {
            Row(
                Modifier
                    .fillMaxWidth()
                    .heightIn(min = GeodeSpacing.TouchTarget)
                    .clickable(enabled = enabled, role = Role.DropdownList, onClickLabel = chooseLabel) { open = true }
                    // TalkBack: "Mode, Step, drop-down list" once, not the value twice (as text and as state).
                    .semantics(mergeDescendants = true) {
                        contentDescription = name
                        stateDescription = value
                    },
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text(name, style = MaterialTheme.typography.bodyMedium)
                if (lockKey != null) KeptMark(lockKey)
                Spacer(Modifier.weight(1f))
                Text(
                    value,
                    modifier = Modifier.padding(start = GeodeSpacing.S).weight(1f, fill = false),
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
                Icon(Icons.Filled.ArrowDropDown, contentDescription = null, tint = MaterialTheme.colorScheme.onSurfaceVariant)
            }
            DropdownMenu(expanded = open, onDismissRequest = { open = false }) {
                options.forEachIndexed { i, label ->
                    DropdownMenuItem(
                        text = { Text(label) },
                        trailingIcon =
                            if (i == selectedIndex) {
                                {
                                    Icon(
                                        Icons.Filled.Check,
                                        contentDescription = stringResource(R.string.customize_a11y_selected),
                                        tint = accentTextColor(),
                                    )
                                }
                            } else {
                                null
                            },
                        onClick = {
                            open = false
                            onSelect(i)
                        },
                    )
                }
            }
        }
        if (enabled && (lockKey != null || about != null)) RowMenu(name, lockKey ?: name, onReset = null, about = about)
    }
}

/** One entry of a [MultiPickButton]'s menu: its name, whether it is on, whether it can be. */
@Immutable
internal class MenuPick(
    val label: String,
    val checked: Boolean,
    val enabled: Boolean = true,
)

/**
 * Several picks from one list (a moment's actions, the ready-made reactions, what an envelope moves)
 * as one button: the [summary] ("Pulse, Zoom") and ▾, opening a menu of checkboxes. The menu stays
 * open while the user ticks, the way a checkable menu does, and closes on a tap outside; a divider
 * goes before each index in [dividersBefore], so a long menu reads as groups.
 * [name] is what TalkBack calls the button; [summary] is read as its state.
 */
@Composable
internal fun MultiPickButton(
    name: String,
    summary: String,
    picks: List<MenuPick>,
    modifier: Modifier = Modifier,
    dividersBefore: Set<Int> = emptySet(),
    onToggle: (Int) -> Unit,
) {
    var open by remember { mutableStateOf(false) }
    Box(modifier) {
        TextButton(
            onClick = { open = true },
            modifier =
                Modifier.semantics {
                    contentDescription = name
                    stateDescription = summary
                },
        ) {
            Text(summary, maxLines = 1, overflow = TextOverflow.Ellipsis, modifier = Modifier.weight(1f, fill = false))
            Icon(Icons.Filled.ArrowDropDown, contentDescription = null)
        }
        DropdownMenu(expanded = open, onDismissRequest = { open = false }) {
            picks.forEachIndexed { i, pick ->
                if (i in dividersBefore) HorizontalDivider(Modifier.padding(vertical = GeodeSpacing.XS))
                DropdownMenuItem(
                    text = { Text(pick.label) },
                    // The trailing box has no click of its own, so the item carries the checked state for TalkBack.
                    modifier =
                        Modifier.semantics {
                            role = Role.Checkbox
                            toggleableState = ToggleableState(pick.checked)
                        },
                    enabled = pick.enabled,
                    trailingIcon = { Checkbox(checked = pick.checked, onCheckedChange = null, enabled = pick.enabled) },
                    onClick = { onToggle(i) },
                )
            }
        }
    }
}

/** A named, keepable pick from a list, as one [DropdownRow]. Hidden as one unit when the parameter is not live here. */
@Composable
internal fun ParamChips(
    label: String,
    options: List<String>,
    selectedIndex: Int,
    scope: ParamScope = ParamScope.of(label),
    onSelect: (Int) -> Unit,
) {
    if (!visible(label, scope)) return
    ProGated(label, value = options.getOrNull(selectedIndex)) {
        DropdownRow(label, options, selectedIndex, lockKey = label, onSelect = onSelect)
    }
}

/**
 * One slider row, two lines and about 80 dp: "Name  value ⋮" over the track. A double tap on the
 * name puts it back to [default]; the ⋮ has Reset and Keep as well.
 *
 * WHY a "Reset" custom action on the name row as well (issue #554's audit): a double tap is a
 * pointer gesture TalkBack cannot make (its own double tap is "activate"), so without it the
 * gesture has no accessible equivalent (WCAG 2.5.1). The action calls the same [reset], and is
 * offered only while the double tap would do something: a value off its default, not Pro-locked.
 *
 * WHY the track's box is at least 48 dp tall (issue #554 row 3): it was forced to 32 dp, which
 * clipped the slider's touch target and let it overlap the rows beside it. `heightIn(min =)` lets
 * the Slider keep its own drawn track and thumb, centred in a target of [GeodeSpacing.TouchTarget]
 * (Compose › API defaults › "Minimum touch target sizes": at least 48 dp, so touch areas cannot
 * overlap). The row grows by 16 dp; nothing is drawn differently.
 *
 * The value goes through [ParamFormat] (a % for amounts, × for multipliers, s/ms, Hz, °) and reads
 * "Default" at the default. A unit suffix in the key ("Particle life (s)") is dropped from the
 * shown name, since the value now carries it.
 *
 * A Pro control the user may not keep is try-then-pay: dimmed, but the slider still moves and the
 * picture follows while it is held; on release it springs back and the paywall offers to keep it.
 */
@Composable
internal fun LabeledSlider(
    label: String,
    value: Float,
    range: ClosedFloatingPointRange<Float>,
    display: String = label,
    scope: ParamScope = ParamScope.of(label),
    default: Float? = null,
    unit: ParamUnit? = null,
    onChange: (Float) -> Unit,
) {
    val name = display.removeSuffix(" (s)").removeSuffix(" (Hz)")
    if (!visible(label, scope, name)) return
    val shown =
        remember(value, default, unit, range) {
            ParamFormat.format(value, unit ?: ParamFormat.unitFor(label, range), default, bipolar = range.start < 0f)
        }
    val lock = LocalProLock.current
    val locked = !lock.allows(label)
    // The value before a locked drag began, put back on release.
    var before by remember(label) { mutableStateOf<Float?>(null) }
    val reset: (() -> Unit)? = default?.takeIf { abs(value - it) > RESET_EPSILON }?.let { d -> { onChange(d) } }
    val resetLabel = stringResource(R.string.customize_a11y_reset)
    val sliderLabel = if (locked) stringResource(R.string.customize_a11y_part_of_pro, name) else name
    Column(Modifier.alpha(if (locked) LOCKED_ALPHA else 1f)) {
        Row(
            modifier =
                Modifier
                    .fillMaxWidth()
                    .heightIn(min = 32.dp)
                    .pointerInput(label, locked, reset) { detectTapGestures(onDoubleTap = { if (!locked) reset?.invoke() }) }
                    // Merged, so the row is one TalkBack stop ("Name, value") that can carry the action;
                    // the ⋮ inside keeps its own stop, being clickable.
                    .semantics(mergeDescendants = true) {
                        val action = reset?.takeIf { !locked }
                        if (action != null) {
                            customActions =
                                listOf(
                                    CustomAccessibilityAction(resetLabel) {
                                        action()
                                        true
                                    },
                                )
                        }
                    },
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(name, style = MaterialTheme.typography.bodyMedium)
            KeptMark(label)
            Spacer(Modifier.weight(1f))
            Text(shown, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
            if (!locked) RowMenu(name, label, reset)
        }
        Slider(
            value = value,
            onValueChange = { v ->
                if (locked && before == null) before = value
                onChange(v)
            },
            valueRange = range,
            onValueChangeFinished =
                if (locked) {
                    {
                        before?.let(onChange)
                        before = null
                        lock.onLocked()
                    }
                } else {
                    null
                },
            // A thumb at 0 sits in the back-gesture strip; without the exclusion a drag from it leaves the screen.
            modifier =
                Modifier.fillMaxWidth().heightIn(min = GeodeSpacing.TouchTarget).systemGestureExclusion().semantics {
                    contentDescription = sliderLabel
                    stateDescription = shown
                },
        )
    }
}

private const val RESET_EPSILON = 1e-4f

@Composable
internal fun CheckRow(
    label: String,
    checked: Boolean,
    display: String = label,
    scope: ParamScope = ParamScope.of(label),
    onChange: (Boolean) -> Unit,
) {
    if (!visible(label, scope, display)) return
    ProGated(label, display, if (checked) "On" else "Off") {
        val proLocked = LocalLockedByPro.current
        // The whole row toggles, so TalkBack reads the label with the box and a tap on the text works.
        Row(
            modifier =
                Modifier
                    .fillMaxWidth()
                    .heightIn(min = 48.dp)
                    .toggleable(value = checked, enabled = !proLocked, role = Role.Checkbox, onValueChange = onChange),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Checkbox(checked = checked, onCheckedChange = null, enabled = !proLocked)
            Text(display, style = MaterialTheme.typography.bodyMedium)
            KeptMark(label)
            Spacer(Modifier.weight(1f))
            if (!proLocked) RowMenu(display, label, onReset = null)
        }
    }
}

@Composable
internal fun LabeledIntSlider(
    label: String,
    value: Int,
    range: IntRange,
    display: String = label,
    scope: ParamScope = ParamScope.of(label),
    onChange: (Int) -> Unit,
) {
    if (!visible(label, scope, display)) return
    ProGated(label, display, "$value") {
        Column(Modifier.padding(vertical = 2.dp)) {
            ControlLabelRow("$display · $value", label)
            Slider(
                value = value.toFloat(),
                onValueChange = { onChange(it.toInt().coerceIn(range.first, range.last)) },
                valueRange = range.first.toFloat()..range.last.toFloat(),
                steps = (range.last - range.first - 1).coerceAtLeast(0),
                enabled = !LocalLockedByPro.current,
                modifier =
                    Modifier.fillMaxWidth().systemGestureExclusion().semantics {
                        contentDescription = display
                        stateDescription = "$value"
                    },
            )
        }
    }
}

@Composable
internal fun CardSubHeader(title: String) {
    if (!sectionVisible()) return
    Text(
        title,
        modifier = Modifier.padding(top = 10.dp, bottom = 2.dp),
        style = MaterialTheme.typography.labelLarge,
        color = accentTextColor(),
    )
}

/** Wraps a tab's content in the scene + search context every control reads. */
@Composable
internal fun ParamSurface(
    sceneId: String,
    query: String,
    content: @Composable () -> Unit,
) = CompositionLocalProvider(
    LocalSceneId provides sceneId,
    LocalParamQuery provides query,
    content = content,
)
