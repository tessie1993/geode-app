package dev.geode.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.minimumInteractiveComponentSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.graphics.luminance
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.data.BootAnimationStore
import dev.geode.data.CornerStyle
import dev.geode.data.FontColorChoice
import dev.geode.data.GuiPrefs
import dev.geode.geodeContainer
import dev.geode.ui.theme.isReadableOnCanvas
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/** Settings › Look › Basics: the accent and the interface sounds. */
@Composable
internal fun LookBasics(viewModel: SettingsViewModel) {
    val gui by viewModel.guiPrefs.collectAsStateWithLifecycle()
    ThemeGroup(viewModel, gui)
}

/** Settings › Look › Text: its size and colour. */
@Composable
internal fun LookTextSection(viewModel: SettingsViewModel) {
    val gui by viewModel.guiPrefs.collectAsStateWithLifecycle()
    TextGroup(viewModel, gui)
}

/** Settings › Look › Layout: corners and the boot animation. */
@Composable
internal fun LookLayoutSection(viewModel: SettingsViewModel) {
    val gui by viewModel.guiPrefs.collectAsStateWithLifecycle()
    LayoutGroup(viewModel, gui)
}

@Composable
private fun ThemeGroup(
    viewModel: SettingsViewModel,
    gui: GuiPrefs,
) {
    Column {
        CrystalSlider(
            label = stringResource(R.string.look_accent_intensity, (gui.accentIntensity * 100).toInt()),
            value = gui.accentIntensity,
            onValueChange = { viewModel.setGuiPrefs(gui.copy(accentIntensity = it)) },
            valueRange = 0.5f..1.5f,
        )
    }
    Column {
        LabeledSwitch(
            stringResource(R.string.look_ui_sounds),
            checked = gui.uiSounds,
            onCheckedChange = { viewModel.setGuiPrefs(gui.copy(uiSounds = it)) },
        )
        Text(
            stringResource(R.string.look_ui_sounds_hint),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

@Composable
private fun TextGroup(
    viewModel: SettingsViewModel,
    gui: GuiPrefs,
) {
    FontColorRow(viewModel, gui)
    Column {
        CrystalSlider(
            label = stringResource(R.string.look_text_size, (gui.textScale * 100).toInt()),
            value = gui.textScale,
            onValueChange = { viewModel.setGuiPrefs(gui.copy(textScale = it)) },
            valueRange = GuiPrefs.TEXT_SCALE_MIN..GuiPrefs.TEXT_SCALE_MAX,
        )
        Text(
            stringResource(R.string.look_text_size_hint),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

@Composable
private fun FontColorRow(
    viewModel: SettingsViewModel,
    gui: GuiPrefs,
) {
    Column {
        Text(stringResource(R.string.look_font_color), style = MaterialTheme.typography.labelMedium)
        LazyRow(
            Modifier.padding(top = 6.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            items(FontColorChoice.CHOICES) { choice ->
                FontColorSwatch(
                    choice = choice,
                    selected = gui.fontColorArgb == choice.argb,
                    onSelect = { viewModel.setGuiPrefs(gui.copy(fontColorArgb = choice.argb)) },
                )
            }
        }
        Text(
            stringResource(R.string.look_font_color_hint),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

/** One text-colour choice. Colours too dark to read on black are shown but can't be picked. */
@Composable
private fun FontColorSwatch(
    choice: FontColorChoice,
    selected: Boolean,
    onSelect: () -> Unit,
) {
    val cs = MaterialTheme.colorScheme
    val usable = choice.argb == null || isReadableOnCanvas(Color(choice.argb))
    val shape = crystalShardShape(8.dp, 3.dp)
    val fill =
        choice.argb?.let { argb ->
            val c = Color(argb)
            Brush.verticalGradient(listOf(c, lerp(c, Color.Black, 0.22f)))
        } ?: Brush.verticalGradient(listOf(cs.primary, cs.secondary))
    Column(
        Modifier
            .minimumInteractiveComponentSize()
            .graphicsLayer { alpha = if (usable) 1f else 0.35f }
            .clickable(enabled = usable, onClick = onSelect)
            .padding(2.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(4.dp),
    ) {
        Box(
            Modifier
                .size(30.dp)
                .clip(shape)
                .background(fill)
                .border(1.dp, if (selected) Color.White.copy(alpha = 0.9f) else cs.outline, shape),
            contentAlignment = Alignment.Center,
        ) {
            if (selected) {
                val swatch = choice.argb?.let { Color(it) } ?: cs.primary
                CrystalGem(if (swatch.luminance() > 0.55f) Color.Black else Color.White, size = 7.dp)
            }
        }
        Text(
            stringResource(choice.labelRes),
            style = MaterialTheme.typography.labelSmall,
            color = if (selected) accentTextColor() else cs.onSurfaceVariant,
            // One line under a 30 dp swatch: at a large font scale the name ends in … instead of being cut (#558).
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
    }
}

@Composable
private fun LayoutGroup(
    viewModel: SettingsViewModel,
    gui: GuiPrefs,
) {
    Column {
        Text(stringResource(R.string.look_corner_style), style = MaterialTheme.typography.labelMedium)
        CrystalSegmented(
            options = CornerStyle.entries.map { stringResource(it.labelRes) },
            selected = CornerStyle.entries.indexOf(gui.cornerStyle),
            onSelect = { viewModel.setGuiPrefs(gui.copy(cornerStyle = CornerStyle.entries[it])) },
            modifier = Modifier.padding(top = 4.dp),
        )
    }
    BootAnimationRow()
}

@Composable
private fun BootAnimationRow() {
    val ctx = LocalContext.current
    val store = remember { BootAnimationStore(ctx.geodeContainer.prefsFiles.general) }
    // Read on IO: opening a prefs file is disk I/O, and this runs in composition. The switch
    // stays disabled for the moment that takes. save() uses apply(), which is already async.
    var bootAnim by remember { mutableStateOf<Boolean?>(null) }
    LaunchedEffect(store) { bootAnim = withContext(Dispatchers.IO) { store.load() } }
    LabeledSwitch(
        stringResource(R.string.look_boot_animation),
        checked = bootAnim ?: true,
        onCheckedChange = {
            bootAnim = it
            store.save(it)
        },
        enabled = bootAnim != null,
    )
}
