package dev.geode.ui.glass

import androidx.compose.ui.graphics.vector.ImageVector

/** One item in a [GlassNavBar]; the same shape as `CrystalNavItem` so `AppShell` can swap it 1:1. */
data class GlassNavItem(
    val label: String,
    val icon: ImageVector,
)
