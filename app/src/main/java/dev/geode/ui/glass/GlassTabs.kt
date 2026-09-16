package dev.geode.ui.glass

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp

/** The vertical pill column from ref-05 ("Home" / "Profile" / …). */
@Composable
fun GlassVerticalTabs(
    titles: List<String>,
    selected: Int,
    onSelect: (Int) -> Unit,
    modifier: Modifier = Modifier,
) {
    Column(modifier, verticalArrangement = Arrangement.spacedBy(10.dp)) {
        titles.forEachIndexed { i, title -> GlassTabPill(title, i == selected) { onSelect(i) } }
    }
}

/** A horizontal pill row, for a tab strip across the top of a screen. */
@Composable
fun GlassHorizontalTabs(
    titles: List<String>,
    selected: Int,
    onSelect: (Int) -> Unit,
    modifier: Modifier = Modifier,
) {
    Row(modifier, horizontalArrangement = Arrangement.spacedBy(10.dp)) {
        titles.forEachIndexed { i, title -> GlassTabPill(title, i == selected) { onSelect(i) } }
    }
}

@Composable
private fun GlassTabPill(
    title: String,
    selected: Boolean,
    onClick: () -> Unit,
) {
    Box(
        Modifier
            .glassSurface(shape = GlassShapes.pill, tint = if (selected) GlassPalette.mint else null, selected = selected)
            .floatOnWater(strength = 0.3f)
            .glassTouch(onClick = onClick)
            .padding(horizontal = 18.dp, vertical = 10.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            title,
            style = MaterialTheme.typography.titleSmall,
            fontWeight = if (selected) FontWeight.SemiBold else FontWeight.Normal,
            color = if (selected) GlassPalette.textPrimary else GlassPalette.textSecondary,
        )
    }
}
