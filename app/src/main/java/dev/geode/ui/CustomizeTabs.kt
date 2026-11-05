package dev.geode.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
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
import androidx.compose.material3.FilledTonalButton
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
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.semantics.toggleableState
import androidx.compose.ui.state.ToggleableState
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import dev.geode.R
import dev.geode.model.ControlRange
import dev.geode.render.fluid.FluidQuality
import dev.geode.render.scene.Catalogue
import dev.geode.render.scene.ParamFormat
import dev.geode.render.scene.ParamKeys
import dev.geode.render.scene.ParamRandomizer
import dev.geode.render.scene.ParamScope
import dev.geode.render.scene.ParamUnit
import dev.geode.render.scene.SceneCapabilities
import dev.geode.render.scene.SceneIds
import dev.geode.render.scene.SceneParams
import dev.geode.ui.theme.GeodeSpacing
import dev.geode.ui.theme.LocalUiSounds
import dev.geode.ui.theme.UiSound
import kotlin.math.abs
import kotlin.reflect.KProperty1

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
    Box {
        CompositionLocalProvider(LocalLockedByPro provides true) {
            // A Column, so a block of several rows (a whole Advanced page in search) still stacks.
            Column(Modifier.alpha(LOCKED_ALPHA).clearAndSetSemantics {}) { content() }
        }
        Box(
            Modifier
                .matchParentSize()
                .clickable(onClickLabel = "Unlock with Pro", role = Role.Button, onClick = lock.onLocked)
                .semantics {
                    contentDescription = "$label, part of Pro"
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
    val lockable = lockKey in ParamRandomizer.LOCKABLE_LABELS
    val reactTo = LocalReactTo.current
    val hasActions = lockable || onReset != null
    if (!hasActions && reactTo == null && about == null) return
    var open by remember { mutableStateOf(false) }
    var explaining by remember { mutableStateOf(false) }
    Box {
        // 40 dp drawn; IconButton still answers to the 48 dp target around it.
        IconButton(onClick = { open = true }, modifier = Modifier.size(40.dp)) {
            Icon(Icons.Filled.MoreVert, contentDescription = "More for $name", tint = MaterialTheme.colorScheme.onSurfaceVariant)
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
        contentDescription = "Kept when randomizing",
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
 * Customize › Look: what every style shares. Colour first (the palette is the free control),
 * then hue, grading and glow, then speed and motion, trails and the screen effects.
 */
@Composable
internal fun LookTab(
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
    onTakeArtworkPalette: (() -> Unit)? = null,
    artworkNote: String? = null,
) {
    val palettes = rememberSavedPalettes()
    Column {
        SectionHeader("Colour")
        if (onTakeArtworkPalette != null && sectionVisible()) {
            ProGated(ParamKeys.PALETTE, "Take the colours from the artwork") {
                val sounds = LocalUiSounds.current
                FilledTonalButton(onClick = {
                    sounds.play(UiSound.TAP)
                    onTakeArtworkPalette()
                }) {
                    Text("Take the colours from the artwork")
                }
            }
            artworkNote?.let { ControlHint(it) }
        }
        if (visible(ParamKeys.PALETTE, ParamScope.UNIVERSAL)) {
            ProGated(ParamKeys.PALETTE) { PaletteDropdown(ParamKeys.PALETTE, ParamKeys.PALETTE, p, onChange, palettes, second = false) }
        }
        if (visible(ParamKeys.COLOUR_MAP, ParamScope.SHADER_LOOK)) {
            val selected = SceneParams.CYCLIC_PALETTES.getOrNull(p.paletteLut) ?: "Off"
            ProGated(ParamKeys.COLOUR_MAP, value = selected) {
                DropdownRow(
                    name = ParamKeys.COLOUR_MAP,
                    options = listOf("Off") + SceneParams.CYCLIC_PALETTES,
                    selectedIndex = if (p.paletteLut < 0) 0 else p.paletteLut + 1,
                    lockKey = ParamKeys.COLOUR_MAP,
                    about = "Even colour steps that loop, so a wrap has no seam.",
                ) { i -> onChange(p.copy(paletteLut = if (i == 0) SceneParams.NO_PALETTE_LUT else i - 1)) }
            }
        }
        LabeledSlider(ParamKeys.PALETTE_BLEND, p, SceneParams::paletteMix) {
            onChange(p.copy(paletteMix = it))
        }
        if (p.paletteMix > 0.001f && visible(ParamKeys.PALETTE_2, ParamScope.SHADER_LOOK)) {
            ProGated(ParamKeys.PALETTE_2) { PaletteDropdown(ParamKeys.PALETTE_2, "Second palette", p, onChange, palettes, second = true) }
        }
        ControlHint(
            "MilkDrop presets paint their own colours. This steers them toward the palette above; 0 leaves the preset untouched.",
            ParamScope.MILKDROP,
        )
        LabeledSlider(
            ParamKeys.MILKDROP_PALETTE_TINT,
            p,
            SceneParams::milkdropPaletteTint,
            display = "Tint toward the palette",
        ) { onChange(p.copy(milkdropPaletteTint = it)) }
        SectionHeader("Hue")
        LabeledSlider(ParamKeys.HUE_SHIFT, p, SceneParams::colorShift) {
            onChange(p.copy(colorShift = it))
        }
        LabeledSlider(ParamKeys.HUE_RANGE, p, SceneParams::hueRange) { onChange(p.copy(hueRange = it)) }
        CheckRow(ParamKeys.COLOR_CYCLE, p.colorCycle, display = "Cycle the colours") { onChange(p.copy(colorCycle = it)) }
        if (p.colorCycle) {
            LabeledSlider(ParamKeys.CYCLE_SPEED, p, SceneParams::cycleSpeed) {
                onChange(p.copy(cycleSpeed = it))
            }
        }
        SectionHeader("Brightness & grading")
        LabeledSlider(ParamKeys.SATURATION, p, SceneParams::saturation) {
            onChange(p.copy(saturation = it))
        }
        // Brightness, intensity and glow stop at the flash clamp (ControlRange.CAPS); past it a slider did nothing.
        LabeledSlider(ParamKeys.BRIGHTNESS, p, SceneParams::brightness) {
            onChange(p.copy(brightness = it))
        }
        LabeledSlider(ParamKeys.CONTRAST, p, SceneParams::contrast) {
            onChange(p.copy(contrast = it))
        }
        LabeledSlider(ParamKeys.GAMMA, p, SceneParams::gamma, display = "Midtones") {
            onChange(p.copy(gamma = it))
        }
        LabeledSlider(ParamKeys.INTENSITY, p, SceneParams::intensity) {
            onChange(p.copy(intensity = it))
        }
        LabeledSlider(ParamKeys.TEMPERATURE, p, SceneParams::temperature, display = "Warmth") {
            onChange(p.copy(temperature = it))
        }
        SectionHeader("Glow & tone")
        LabeledSlider(ParamKeys.BLOOM, p, SceneParams::bloom, display = "Glow") {
            onChange(p.copy(bloom = it))
        }
        LabeledSlider(ParamKeys.POSTERIZE, p, SceneParams::posterize, display = "Fewer colours") {
            onChange(p.copy(posterize = it))
        }
        CheckRow(ParamKeys.DUOTONE, p.duotone, display = "Two-colour") { onChange(p.copy(duotone = it)) }
        CheckRow(ParamKeys.SOLARIZE, p.solarize) { onChange(p.copy(solarize = it)) }
        CheckRow(ParamKeys.INVERT, p.invert, display = "Invert colours") { onChange(p.copy(invert = it)) }
        LookEffectSections(p, onChange)
    }
}

/**
 * A palette pick ([second] for the blend's second slot) as one dropdown row, the built-in palettes then
 * the user's own, with the gradient it paints under it. The chip wall it replaces is still
 * [PaletteSlotSelector] in the palette maker, which owns that page.
 */
@Composable
private fun PaletteDropdown(
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
 * Motion › Movement: how fast the picture runs and how it drifts. The engine's Motion roll group,
 * which [dev.geode.render.scene.CustomizeTab.MOTION] rolls and resets as a page.
 */
@Composable
internal fun MovementSections(
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
) {
    SectionHeader("Movement")
    LabeledSlider(ParamKeys.SPEED, p, SceneParams::speed) { onChange(p.copy(speed = it)) }
    LabeledSlider(ParamKeys.ZOOM, p, SceneParams::zoom) { onChange(p.copy(zoom = it)) }
    LabeledSlider(ParamKeys.ROTATION, p, SceneParams::rotation, display = "Spin") {
        onChange(p.copy(rotation = it))
    }
    LabeledSlider(ParamKeys.SWAY, p, SceneParams::sway) { onChange(p.copy(sway = it)) }
    LabeledSlider(ParamKeys.DRIFT_X, p, SceneParams::driftX, display = "Drift sideways") {
        onChange(p.copy(driftX = it))
    }
    LabeledSlider(ParamKeys.DRIFT_Y, p, SceneParams::driftY, display = "Drift up and down") {
        onChange(p.copy(driftY = it))
    }
}

/** Look's second half: trails and the screen effects (the engine's Fx group). */
@Composable
private fun LookEffectSections(
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
) {
    SectionHeader("Trails", ParamScope.TRAIL_LENGTH)
    CheckRow(ParamKeys.TRAILS, p.trails) { onChange(p.copy(trails = it)) }
    LabeledSlider(ParamKeys.TRAIL_LENGTH, p, SceneParams::trailLength) {
        onChange(p.copy(trailLength = it))
    }
    LabeledSlider(
        ParamKeys.TRAIL_ZOOM_ECHO_IN_OUT,
        p,
        SceneParams::trailZoom,
        display = "Trail zoom",
    ) { onChange(p.copy(trailZoom = it)) }
    LabeledSlider(
        ParamKeys.TRAIL_WARP_LIQUID_ECHO,
        p,
        SceneParams::trailWarp,
        display = "Trail ripple",
    ) { onChange(p.copy(trailWarp = it)) }
    SectionHeader("Screen effects")
    LabeledSlider(
        ParamKeys.CHROMATIC_ABERRATION,
        p,
        SceneParams::chromaAb,
        display = "Colour fringe",
    ) { onChange(p.copy(chromaAb = it)) }
    LabeledSlider(ParamKeys.VIGNETTE, p, SceneParams::vignette, display = "Dark edges") {
        onChange(p.copy(vignette = it))
    }
    LabeledSlider(ParamKeys.SCANLINES, p, SceneParams::scanlines) {
        onChange(p.copy(scanlines = it))
    }
    LabeledSlider(ParamKeys.FILM_GRAIN, p, SceneParams::grain) { onChange(p.copy(grain = it)) }
    LabeledSlider(ParamKeys.FISHEYE, p, SceneParams::fisheye, display = "Bulge") {
        onChange(p.copy(fisheye = it))
    }
}

/**
 * Whether the style page has anything to show for [sceneId].
 *
 * Shape and symmetry reach every style today (the composite pass applies them to the finished
 * frame), so the page is always there; the check stays so a style that ever loses them loses the
 * page with them instead of showing an empty tab.
 */
internal fun styleTabApplies(sceneId: String): Boolean = STYLE_SCOPES.any { it.appliesTo(sceneId) }

/**
 * Motion › Zoom & dive and Inner motion: the motion only some styles have. They hide themselves
 * through [ParamScope] on a style without them, like every control.
 */
@Composable
internal fun StyleMotionSections(
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
) {
    SectionHeader("Zoom & dive", ParamScope.ENDLESS_ZOOM)
    ControlHint("A dive that never arrives. Dive speed sets the rate.", ParamScope.ENDLESS_ZOOM)
    CheckRow(ParamKeys.ENDLESS_ZOOM, p.endlessZoom) { onChange(p.copy(endlessZoom = it)) }
    if (p.endlessZoom) {
        LabeledSlider(ParamKeys.DIVE_SPEED, p, SceneParams::endlessZoomSpeed) {
            onChange(p.copy(endlessZoomSpeed = it))
        }
    }
    SectionHeader("Inner motion", ParamScope.TURBULENCE)
    ControlHint("A force inside the scene, not a screen effect.", ParamScope.TURBULENCE)
    LabeledSlider(ParamKeys.TURBULENCE, p, SceneParams::turbulence) {
        onChange(p.copy(turbulence = it))
    }
}

private val STYLE_SCOPES =
    listOf(
        ParamScope.UNIVERSAL,
        ParamScope.SHADER_LOOK,
        ParamScope.MARCH_DETAIL,
        ParamScope.PARTICLE_SPRITE,
        ParamScope.TURBULENCE,
        ParamScope.ENDLESS_ZOOM,
        ParamScope.MILKDROP,
        ParamScope.FLUID_SIM,
        ParamScope.WATER,
        ParamScope.CYMATICS,
    )

/**
 * Customize › Style: the controls that belong to the style rather than to every style; its motion
 * (the endless dive, inner motion) is on Motion with the rest of what moves.
 * Each section hides itself through [ParamScope] when the style on screen cannot show it, and the
 * Fluid and Cymatics sections appear only on their own kind of style.
 */
@Composable
internal fun StyleTab(
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
) {
    val sceneId = LocalSceneId.current
    Column {
        SectionHeader("Shape")
        LabeledSlider(ParamKeys.DOMAIN_WARP, p, SceneParams::warp, display = "Warp") {
            onChange(p.copy(warp = it))
        }
        LabeledSlider(ParamKeys.RIPPLE, p, SceneParams::ripple) { onChange(p.copy(ripple = it)) }
        LabeledSlider(ParamKeys.MORPH, p, SceneParams::morph) { onChange(p.copy(morph = it)) }
        LabeledSlider(ParamKeys.TWIST, p, SceneParams::twist) { onChange(p.copy(twist = it)) }
        SectionHeader("Symmetry & tiling")
        CheckRow(ParamKeys.KALEIDOSCOPE, p.kaleidoscope) { on ->
            val folds = if (on && p.symmetry < 2) SceneParams.DEFAULT_SYMMETRY_FOLDS else p.symmetry
            onChange(p.copy(kaleidoscope = on, symmetry = folds))
        }
        if (p.kaleidoscope && visible(ParamKeys.KALEIDOSCOPE, ParamScope.of(ParamKeys.KALEIDOSCOPE))) {
            val folds = SceneParams.SYMMETRY_FOLDS.filter { it >= 2 }
            ProGated(ParamKeys.KALEIDOSCOPE, "Folds", "${p.symmetry}") {
                DropdownRow("Folds", folds.map { "$it" }, selectedIndex = folds.indexOf(p.symmetry)) { idx ->
                    onChange(p.copy(symmetry = folds[idx]))
                }
            }
        }
        CheckRow(ParamKeys.MIRROR, p.mirror) { onChange(p.copy(mirror = it)) }
        LabeledSlider(ParamKeys.TILE, p, SceneParams::tile) { onChange(p.copy(tile = it)) }
        LabeledSlider(ParamKeys.PIXELATE, p, SceneParams::pixelate) { onChange(p.copy(pixelate = it)) }
        SectionHeader("Detail", ParamScope.MARCH_DETAIL)
        ControlHint("Lower it on slower phones: the picture stays, the edges soften.", ParamScope.MARCH_DETAIL)
        LabeledSlider(
            ParamKeys.MARCH_DETAIL,
            p,
            SceneParams::marchDetail,
        ) { onChange(p.copy(marchDetail = it)) }
        SectionHeader("Particles", ParamScope.PARTICLE_SPRITE)
        ParamChips(ParamKeys.PARTICLE_SHAPE, SceneParams.PARTICLE_SHAPES, p.particleShape) {
            onChange(p.copy(particleShape = it))
        }
        LabeledSlider(ParamKeys.PARTICLE_SIZE, p, SceneParams::particleSize) {
            onChange(p.copy(particleSize = it))
        }
        LabeledSlider(ParamKeys.DENSITY, p, SceneParams::density) { onChange(p.copy(density = it)) }
        SectionHeader("MilkDrop", ParamScope.MILKDROP)
        CheckRow(ParamKeys.BLEND_PRESET_CHANGES, p.milkdropBlendPresets) { onChange(p.copy(milkdropBlendPresets = it)) }
        if (SceneCapabilities.isFluid(sceneId) || ParamScope.WATER.appliesTo(sceneId) || ParamScope.JOURNEY.appliesTo(sceneId)) {
            FluidStyleSections(p, onChange)
        }
        if (SceneCapabilities.isCymatics(sceneId)) CymaticsSections(p, onChange)
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
    Row(Modifier.fillMaxWidth().heightIn(min = GeodeSpacing.TouchTarget), verticalAlignment = Alignment.CenterVertically) {
        Box(Modifier.weight(1f)) {
            Row(
                Modifier
                    .fillMaxWidth()
                    .heightIn(min = GeodeSpacing.TouchTarget)
                    .clickable(enabled = enabled, role = Role.DropdownList, onClickLabel = "Choose $name") { open = true }
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
                                { Icon(Icons.Filled.Check, contentDescription = "Selected", tint = accentTextColor()) }
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
 * One slider row, two lines and about 64 dp: "Name  value ⋮" over the track. A double tap on the
 * name puts it back to [default]; the ⋮ has Reset and Keep as well.
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
    Column(Modifier.alpha(if (locked) LOCKED_ALPHA else 1f)) {
        Row(
            modifier =
                Modifier
                    .fillMaxWidth()
                    .heightIn(min = 32.dp)
                    .pointerInput(label, locked, reset) { detectTapGestures(onDoubleTap = { if (!locked) reset?.invoke() }) },
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
                Modifier.fillMaxWidth().height(32.dp).systemGestureExclusion().semantics {
                    contentDescription = if (locked) "$name, part of Pro" else name
                    stateDescription = shown
                },
        )
    }
}

private const val RESET_EPSILON = 1e-4f

/**
 * A [LabeledSlider] for one scene param: the value is [params]'s, the range the schema's
 * ([ControlRange.of], with its few UI caps), and Reset goes to [SceneParams.DEFAULT]'s. The
 * row keeps only what is UI, its label and where it sits; no slider writes a range of its own (#346).
 */
@Composable
internal fun LabeledSlider(
    label: String,
    params: SceneParams,
    param: KProperty1<SceneParams, Float>,
    display: String = label,
    scope: ParamScope = ParamScope.of(label),
    unit: ParamUnit? = null,
    onChange: (Float) -> Unit,
) {
    LabeledSlider(
        label = label,
        value = param.get(params),
        range = ControlRange.of(param),
        display = display,
        scope = scope,
        default = param.get(SceneParams.DEFAULT),
        unit = unit,
        onChange = onChange,
    )
}

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

/** A [LabeledIntSlider] for one whole-number scene param: its value from [params], its range the schema's. */
@Composable
internal fun LabeledIntSlider(
    label: String,
    params: SceneParams,
    param: KProperty1<SceneParams, Int>,
    display: String = label,
    scope: ParamScope = ParamScope.of(label),
    onChange: (Int) -> Unit,
) {
    val range = ControlRange.of(param)
    LabeledIntSlider(label, param.get(params), range.start.toInt()..range.endInclusive.toInt(), display, scope, onChange)
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

/**
 * The style page's Fluid, Water and particle-journey sections. Every one hides itself through its
 * [ParamScope], and the solver's own sections also ask [SceneCapabilities.isFluid], so a water or
 * curl-flow style shows only what it actually reads.
 */
@Composable
private fun FluidStyleSections(
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
) {
    val sceneId = LocalSceneId.current
    val isFluidScene = SceneCapabilities.isFluid(sceneId)
    SectionHeader("Water", ParamScope.WATER)
    ControlHint("Hits drop expanding rings, stirrers carve wakes, the journey decides where they land.", ParamScope.WATER)
    LabeledSlider(ParamKeys.RIPPLE_STRENGTH, p, SceneParams::waterRippleStrength) {
        onChange(p.copy(waterRippleStrength = it))
    }
    LabeledSlider(ParamKeys.DEPTH, p, SceneParams::waterDepth) { onChange(p.copy(waterDepth = it)) }
    LabeledSlider(ParamKeys.SPECULAR, p, SceneParams::waterSpecular, display = "Shine") {
        onChange(p.copy(waterSpecular = it))
    }
    LabeledSlider(ParamKeys.FLOW_DRIFT, p, SceneParams::waterFlow) { onChange(p.copy(waterFlow = it)) }
    if (ParamScope.WATER.appliesTo(sceneId)) {
        LabeledSlider(ParamKeys.WAVE_SPEED, p, SceneParams::waterWaveSpeed) {
            onChange(p.copy(waterWaveSpeed = it))
        }
        LabeledSlider(ParamKeys.DAMPING, p, SceneParams::waterDamping, display = "Calming") {
            onChange(p.copy(waterDamping = it))
        }
    }
    SectionHeader(ParamKeys.LIQUID, ParamScope.WATER)
    ControlHint("Every splash stains a colour film the surface carries. Turn it down for a plain pool.", ParamScope.WATER)
    LabeledSlider(ParamKeys.LIQUID, p, SceneParams::waterLiquid) { onChange(p.copy(waterLiquid = it)) }
    if (p.waterLiquid > 0.001f) {
        LabeledSlider(ParamKeys.LIQUID_FLOW, p, SceneParams::waterLiquidFlow) {
            onChange(p.copy(waterLiquidFlow = it))
        }
        LabeledSlider(ParamKeys.LIQUID_FADE, p, SceneParams::waterLiquidFade) {
            onChange(p.copy(waterLiquidFade = it))
        }
    }
    SectionHeader("Journey", ParamScope.JOURNEY)
    ControlHint(
        "Spawn points give birth to particles and colour; catch points pull them in. The music walks both around the screen.",
        ParamScope.JOURNEY,
    )
    ParamChips(ParamKeys.PATH, SceneParams.FLUID_PATHS, p.fluidSpawnPath.coerceIn(0, SceneParams.FLUID_PATHS.size - 1)) {
        onChange(p.copy(fluidSpawnPath = it))
    }
    LabeledIntSlider(ParamKeys.SPAWN_POINTS, p, SceneParams::fluidSpawnPoints) { onChange(p.copy(fluidSpawnPoints = it)) }
    LabeledSlider(PROGRESSION, p, SceneParams::fluidSpawnProgress, scope = ParamScope.JOURNEY) {
        onChange(p.copy(fluidSpawnProgress = it))
    }
    LabeledIntSlider(ParamKeys.CATCH_POINTS, p, SceneParams::fluidCatchPoints) { onChange(p.copy(fluidCatchPoints = it)) }
    if (p.fluidCatchPoints > 0) {
        LabeledSlider(ParamKeys.CATCH_PULL, p, SceneParams::fluidCatchPull) {
            onChange(p.copy(fluidCatchPull = it))
        }
        LabeledSlider(ParamKeys.CATCH_RADIUS, p, SceneParams::fluidCatchRadius) {
            onChange(p.copy(fluidCatchRadius = it))
        }
    }
    if (sectionVisible(ParamScope.EMITTERS)) {
        SectionHeader("Quality", ParamScope.EMITTERS)
        val labels = FluidQuality.LABELS
        val quality = p.fluidQuality.coerceIn(0, labels.size - 1)
        ProGated(FLUID_QUALITY, value = labels[quality]) {
            DropdownRow(FLUID_QUALITY, labels, quality) { onChange(p.copy(fluidQuality = it)) }
        }
        CheckRow(AUTO_QUALITY, p.fluidAutoQuality, scope = ParamScope.EMITTERS) { onChange(p.copy(fluidAutoQuality = it)) }
    }
    LabeledIntSlider(ParamKeys.SOLVER_ITERATIONS, p, SceneParams::fluidIterations, display = "Accuracy") {
        onChange(p.copy(fluidIterations = it))
    }
    if (isFluidScene) FluidSolverSections(p, onChange)
    SectionHeader("Emitters", ParamScope.EMITTERS)
    ParamChips(ParamKeys.BEAT_PATTERN, SceneParams.FLUID_PATTERNS, p.fluidBeatPattern.coerceIn(0, 3)) {
        onChange(p.copy(fluidBeatPattern = it))
    }
    LabeledIntSlider(ParamKeys.BEAT_SPLATS, p, SceneParams::fluidBeatSplats) { onChange(p.copy(fluidBeatSplats = it)) }
    LabeledIntSlider(ParamKeys.STIRRERS, p, SceneParams::fluidStirrers) { onChange(p.copy(fluidStirrers = it)) }
    LabeledSlider(ParamKeys.STIRRER_SPEED, p, SceneParams::fluidStirrerSpeed) {
        onChange(p.copy(fluidStirrerSpeed = it))
    }
    LabeledSlider(ParamKeys.FLUID_SPLAT_RADIUS, p, SceneParams::fluidSplatRadius) {
        onChange(p.copy(fluidSplatRadius = it))
    }
    LabeledSlider(ParamKeys.RADIUS_ON_BEAT, p, SceneParams::fluidRadiusPulse) {
        onChange(p.copy(fluidRadiusPulse = it))
    }
    LabeledSlider(ParamKeys.FLUID_SPLAT_FORCE, p, SceneParams::fluidSplatForce) {
        onChange(p.copy(fluidSplatForce = it))
    }
    CheckRow(ParamKeys.BASS_PUMP, p.fluidBassPump) { onChange(p.copy(fluidBassPump = it)) }
    CheckRow(ParamKeys.TREBLE_SPARKLE, p.fluidSparkle) { onChange(p.copy(fluidSparkle = it)) }
    LabeledSlider(ParamKeys.PALETTE_CYCLE, p, SceneParams::fluidPaletteCycleSpeed) {
        onChange(p.copy(fluidPaletteCycleSpeed = it))
    }
    SectionHeader("Fluid particles", ParamScope.PARTICLE_SPRITE)
    if (isFluidScene) {
        CheckRow(PARTICLE_LAYER, p.fluidParticlesEnabled, scope = ParamScope.FLUID_SIM) { onChange(p.copy(fluidParticlesEnabled = it)) }
    }
    if (!isFluidScene || p.fluidParticlesEnabled) {
        LabeledSlider(ParamKeys.PARTICLE_DRAG, p, SceneParams::fluidParticleDrag) {
            onChange(p.copy(fluidParticleDrag = it))
        }
        LabeledSlider(ParamKeys.PARTICLE_LIFE_S, p, SceneParams::fluidParticleLife) {
            onChange(p.copy(fluidParticleLife = it))
        }
        LabeledSlider(
            ParamKeys.PARTICLE_BRIGHTNESS,
            p,
            SceneParams::fluidParticleBrightness,
        ) { onChange(p.copy(fluidParticleBrightness = it)) }
    }
    if (isFluidScene) {
        CheckRow(INK_LAYER, p.fluidDyeEnabled, scope = ParamScope.FLUID_SIM) { onChange(p.copy(fluidDyeEnabled = it)) }
    }
}

/** The Fluid solver's own sections: only ever drawn on a Fluid style. */
@Composable
private fun FluidSolverSections(
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
) {
    SectionHeader("Character", ParamScope.FLUID_SIM)
    LabeledSlider(ParamKeys.FLUID_CURL, p, SceneParams::fluidCurl, display = "Swirl") {
        onChange(p.copy(fluidCurl = it))
    }
    LabeledSlider(ParamKeys.MOTION_FADE, p, SceneParams::fluidVelocityDissipation) {
        onChange(p.copy(fluidVelocityDissipation = it))
    }
    LabeledSlider(ParamKeys.FLUID_FADE, p, SceneParams::fluidDensityDissipation) {
        onChange(p.copy(fluidDensityDissipation = it))
    }
    LabeledSlider(
        ParamKeys.CHROMATIC_AGING,
        p,
        SceneParams::fluidChromaticAging,
        display = "Colour ageing",
    ) {
        onChange(p.copy(fluidChromaticAging = it))
    }
    LabeledSlider(ParamKeys.PRESSURE, p, SceneParams::fluidPressure) {
        onChange(p.copy(fluidPressure = it))
    }
    SectionHeader("Fluid look", ParamScope.FLUID_SIM)
    CheckRow(ParamKeys.SHADING_EMBOSSED_INK, p.fluidShading, display = "Embossed ink") { onChange(p.copy(fluidShading = it)) }
    CheckRow(ParamKeys.GLOW_FLUID, p.fluidBloom, display = "Glow") { onChange(p.copy(fluidBloom = it)) }
    if (p.fluidBloom) {
        LabeledSlider(
            ParamKeys.FLUID_GLOW,
            p,
            SceneParams::fluidBloomIntensity,
            display = "Glow amount",
        ) {
            onChange(p.copy(fluidBloomIntensity = it))
        }
        LabeledSlider(ParamKeys.GLOW_THRESHOLD, p, SceneParams::fluidBloomThreshold) {
            onChange(p.copy(fluidBloomThreshold = it))
        }
    }
    CheckRow(ParamKeys.SUNRAYS, p.fluidSunrays) { onChange(p.copy(fluidSunrays = it)) }
    if (p.fluidSunrays) {
        LabeledSlider(
            ParamKeys.SUNRAYS_WEIGHT,
            p,
            SceneParams::fluidSunraysWeight,
            display = "Sunray strength",
        ) {
            onChange(p.copy(fluidSunraysWeight = it))
        }
    }
    SectionHeader("What the music moves", ParamScope.FLUID_SIM)
    LabeledSlider(
        ParamKeys.CURL_FROM_MIDS,
        p,
        SceneParams::fluidCurlAudio,
        display = "Swirl from the mids",
    ) {
        onChange(p.copy(fluidCurlAudio = it))
    }
    LabeledSlider(
        ParamKeys.GLOW_FROM_LOUDNESS,
        p,
        SceneParams::fluidBloomAudio,
        display = "Glow from volume",
    ) {
        onChange(p.copy(fluidBloomAudio = it))
    }
    LabeledSlider(ParamKeys.FADE_WHEN_QUIET, p, SceneParams::fluidFadeAudio) {
        onChange(p.copy(fluidFadeAudio = it))
    }
}

@Composable
private fun CymaticsSections(
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
) {
    val activeSceneId = LocalSceneId.current
    SectionHeader("Wave", ParamScope.CYMATICS)
    ControlHint(
        "The standing-wave pattern of whatever is playing. A pure tone draws one clean figure, a chord the sum of its notes.",
        ParamScope.CYMATICS,
    )
    val forcedGeometry =
        Catalogue
            .override(activeSceneId, ParamKeys.GEOMETRY)
            ?.let { SceneParams.CYMATICS_GEOMETRIES.getOrNull(it.toInt()) }
    if (forcedGeometry != null) {
        ControlHint("Shape set by this style: $forcedGeometry.", ParamScope.CYMATICS)
    } else {
        ParamChips(ParamKeys.GEOMETRY, SceneParams.CYMATICS_GEOMETRIES, p.cymaticsGeometry) { onChange(p.copy(cymaticsGeometry = it)) }
    }
    LabeledSlider(
        ParamKeys.FUNDAMENTAL_HZ,
        p.cymaticsFundamental,
        ControlRange.of(SceneParams::cymaticsFundamental),
        display = "Base note (Hz)",
    ) {
        onChange(p.copy(cymaticsFundamental = it))
    }
    LabeledIntSlider(ParamKeys.STANDING_WAVES, p, SceneParams::cymaticsModes) {
        onChange(p.copy(cymaticsModes = it))
    }
    LabeledSlider(ParamKeys.TONAL_FOCUS, p, SceneParams::cymaticsFocus) {
        onChange(p.copy(cymaticsFocus = it))
    }
    LabeledSlider(ParamKeys.PLATE_RING, p, SceneParams::cymaticsRing) {
        onChange(p.copy(cymaticsRing = it))
    }
    SectionHeader("Field", ParamScope.CYMATICS)
    ControlHint("How much of the wave pattern fills the screen, and how it moves through it.", ParamScope.CYMATICS)
    LabeledSlider(ParamKeys.FIELD_SCALE, p, SceneParams::cymaticsScale) {
        onChange(p.copy(cymaticsScale = it))
    }
    LabeledSlider(ParamKeys.WAVE_FLOW, p, SceneParams::cymaticsFlow) {
        onChange(p.copy(cymaticsFlow = it))
    }
    LabeledSlider(ParamKeys.FIELD_SWIRL, p, SceneParams::cymaticsSwirl) {
        onChange(p.copy(cymaticsSwirl = it))
    }
    SectionHeader("Surface", ParamScope.CYMATICS)
    ControlHint("Fill runs from bare lines on dark cells to a fully filled, shimmering surface.", ParamScope.CYMATICS)
    LabeledSlider(ParamKeys.FILL, p, SceneParams::cymaticsFill) {
        onChange(p.copy(cymaticsFill = it))
    }
    LabeledSlider(ParamKeys.NODAL_LINES, p, SceneParams::cymaticsLine, display = "Still lines") {
        onChange(p.copy(cymaticsLine = it))
    }
    LabeledSlider(ParamKeys.NODAL_GLOW, p, SceneParams::cymaticsGlow, display = "Line glow") {
        onChange(p.copy(cymaticsGlow = it))
    }
    LabeledSlider(
        ParamKeys.IRIDESCENCE,
        p,
        SceneParams::cymaticsIridescence,
        display = "Rainbow sheen",
    ) {
        onChange(p.copy(cymaticsIridescence = it))
    }
    LabeledSlider(
        ParamKeys.CAUSTIC_SHEEN,
        p,
        SceneParams::cymaticsCaustic,
        display = "Light ripples",
    ) {
        onChange(p.copy(cymaticsCaustic = it))
    }
}

// Names for the controls that are panel machinery rather than scene parameters. They live in one
// place so the search matches them and nothing has to spell a string twice.
private const val PROGRESSION = "Progression"
private const val FLUID_QUALITY = "Fluid quality"
private const val AUTO_QUALITY = "Lower the quality when the phone struggles"
private const val PARTICLE_LAYER = "Particle layer"
private const val INK_LAYER = "Ink layer"

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
