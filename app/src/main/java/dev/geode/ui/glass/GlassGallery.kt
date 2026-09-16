package dev.geode.ui.glass

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import dev.geode.R

/**
 * Shows every liquid-glass primitive once, over the water background, so a screen unit can check
 * its work against this one place. Not wired to any route.
 */
@Composable
fun GlassGallery(modifier: Modifier = Modifier) {
    Box(modifier.fillMaxSize()) {
        LiquidBackground(Modifier.fillMaxSize())
        LazyColumn(
            Modifier.fillMaxSize().padding(20.dp),
            verticalArrangement = Arrangement.spacedBy(24.dp),
        ) {
            item { Text(stringResource(R.string.glass_gallery_title), style = MaterialTheme.typography.displaySmall) }
            item { GallerySection(R.string.glass_gallery_section_top_bar) { GalleryTopBar() } }
            item { GallerySection(R.string.glass_gallery_section_buttons) { GalleryButtons() } }
            item { GallerySection(R.string.glass_gallery_section_slider) { GallerySlider() } }
            item { GallerySection(R.string.glass_gallery_section_toggle) { GalleryToggle() } }
            item { GallerySection(R.string.glass_gallery_section_knob) { GalleryKnob() } }
            item { GallerySection(R.string.glass_gallery_section_progress) { GalleryProgress() } }
            item { GallerySection(R.string.glass_gallery_section_list_row) { GalleryListRow() } }
            item { GallerySection(R.string.glass_gallery_section_tabs) { GalleryTabs() } }
            item { GallerySection(R.string.glass_gallery_section_segmented) { GallerySegmented() } }
            item { GallerySection(R.string.glass_gallery_section_nav_bar) { GalleryNavBar() } }
            item { GallerySection(R.string.glass_gallery_section_text_field) { GalleryTextField() } }
            item { GallerySection(R.string.glass_gallery_section_dialog) { GalleryDialog() } }
            item { GallerySection(R.string.glass_gallery_section_transport_bar) { GalleryTransportBar() } }
        }
    }
}

@Composable
private fun GallerySection(
    titleRes: Int,
    content: @Composable () -> Unit,
) {
    Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Text(stringResource(titleRes), style = MaterialTheme.typography.titleMedium, color = GlassPalette.textPrimary)
        content()
    }
}

@Composable
private fun GalleryTopBar() {
    GlassTopBar(title = stringResource(R.string.app_name), onClose = {}, onMenu = {})
}

@Composable
private fun GalleryButtons() {
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
        GlassButton(text = stringResource(R.string.glass_gallery_button_label), onClick = {}, icon = GlassIcons.Heart)
        GlassBubbleButton(icon = GlassIcons.Star, contentDescription = null, onClick = {})
        GlassTile(onClick = {}) { Icon(GlassIcons.Camera, contentDescription = null) }
        GlassPlayButton(icon = GlassIcons.Play, contentDescription = stringResource(R.string.action_play), onClick = {})
    }
}

@Composable
private fun GallerySlider() {
    var value by remember { mutableFloatStateOf(0.4f) }
    GlassSlider(value = value, onValueChange = { value = it }, modifier = Modifier.fillMaxWidth())
}

@Composable
private fun GalleryToggle() {
    var checked by remember { mutableStateOf(true) }
    GlassToggle(checked = checked, onCheckedChange = { checked = it })
}

@Composable
private fun GalleryKnob() {
    var value by remember { mutableFloatStateOf(0.6f) }
    GlassKnob(value = value, onValueChange = { value = it })
}

@Composable
private fun GalleryProgress() {
    Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
        GlassLinearProgress(progress = 0.6f, modifier = Modifier.fillMaxWidth())
        GlassCircularProgress(progress = 0.4f)
    }
}

@Composable
private fun GalleryListRow() {
    GlassListRow(
        title = stringResource(R.string.glass_gallery_list_row_title),
        subtitle = stringResource(R.string.glass_gallery_list_row_subtitle),
        onClick = {},
    )
}

@Composable
private fun GalleryTabs() {
    var selected by remember { mutableIntStateOf(0) }
    val titles =
        listOf(
            stringResource(R.string.nav_player),
            stringResource(R.string.nav_library),
            stringResource(R.string.nav_visuals),
        )
    GlassHorizontalTabs(titles = titles, selected = selected, onSelect = { selected = it })
}

@Composable
private fun GallerySegmented() {
    var selected by remember { mutableIntStateOf(0) }
    val options =
        listOf(
            stringResource(R.string.glass_gallery_segment_one),
            stringResource(R.string.glass_gallery_segment_two),
            stringResource(R.string.glass_gallery_segment_three),
        )
    GlassSegmented(options = options, selected = selected, onSelect = { selected = it }, modifier = Modifier.fillMaxWidth())
}

@Composable
private fun GalleryNavBar() {
    var selected by remember { mutableIntStateOf(0) }
    val items =
        listOf(
            GlassNavItem(stringResource(R.string.nav_player), GlassIcons.Play),
            GlassNavItem(stringResource(R.string.nav_library), GlassIcons.ListIcon),
            GlassNavItem(stringResource(R.string.nav_visuals), GlassIcons.Settings),
        )
    GlassNavBar(items = items, selected = selected, onSelect = { selected = it })
}

@Composable
private fun GalleryTextField() {
    var text by remember { mutableStateOf("") }
    GlassTextField(
        value = text,
        onValueChange = { text = it },
        placeholder = stringResource(R.string.glass_gallery_text_field_placeholder),
        modifier = Modifier.fillMaxWidth(),
    )
}

@Composable
private fun GalleryDialog() {
    var open by remember { mutableStateOf(false) }
    GlassButton(text = stringResource(R.string.glass_gallery_dialog_open), onClick = { open = true })
    if (open) {
        GlassDialog(
            onDismissRequest = { open = false },
            title = stringResource(R.string.glass_gallery_dialog_title),
            text = stringResource(R.string.glass_gallery_dialog_message),
            actions = { GlassButton(text = stringResource(R.string.action_close), onClick = { open = false }) },
        )
    }
}

@Composable
private fun GalleryTransportBar() {
    var playing by remember { mutableStateOf(false) }
    GlassTransportBar(
        playing = playing,
        onPlayPause = { playing = !playing },
        onPrevious = {},
        onNext = {},
        onLibrary = {},
        onProfile = {},
    )
}
