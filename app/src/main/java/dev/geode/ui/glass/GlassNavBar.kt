package dev.geode.ui.glass

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/** One item in a [GlassNavBar]; the same shape as `CrystalNavItem` so `AppShell` can swap it 1:1. */
data class GlassNavItem(
    val label: String,
    val icon: ImageVector,
)

/** A bottom row of glass bubbles with labels. */
@Composable
fun GlassNavBar(
    items: List<GlassNavItem>,
    selected: Int,
    onSelect: (Int) -> Unit,
    modifier: Modifier = Modifier,
    opacity: Float = 1f,
) {
    Box(
        modifier
            .fillMaxWidth()
            .glassSurface(shape = GlassShapes.sheet)
            .floatOnWater(strength = 0.15f),
    ) {
        Row(
            Modifier
                .fillMaxWidth()
                .windowInsetsPadding(WindowInsets.navigationBars)
                .height(68.dp)
                .selectableGroup(),
        ) {
            items.forEachIndexed { i, item ->
                val sel = i == selected
                Column(
                    Modifier
                        .weight(1f)
                        .fillMaxHeight()
                        .selectable(selected = sel, role = Role.Tab, onClick = { onSelect(i) }),
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.Center,
                ) {
                    val tint = if (sel) GlassPalette.textPrimary else GlassPalette.textSecondary
                    Icon(item.icon, contentDescription = item.label, tint = tint)
                    Text(
                        item.label,
                        style = MaterialTheme.typography.labelSmall.copy(letterSpacing = 1.2.sp),
                        fontWeight = if (sel) FontWeight.SemiBold else FontWeight.Normal,
                        color = tint,
                    )
                }
            }
        }
    }
}
