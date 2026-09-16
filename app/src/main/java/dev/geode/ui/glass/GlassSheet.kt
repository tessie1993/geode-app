package dev.geode.ui.glass

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.SheetState
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp

/** A bottom sheet with the glass container colour and a pearl drag handle. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun GlassSheet(
    onDismissRequest: () -> Unit,
    modifier: Modifier = Modifier,
    sheetState: SheetState = rememberModalBottomSheetState(),
    content: @Composable () -> Unit,
) {
    ModalBottomSheet(
        onDismissRequest = onDismissRequest,
        modifier = modifier,
        sheetState = sheetState,
        containerColor = GlassPalette.baseLight,
        contentColor = GlassPalette.textPrimary,
        dragHandle = { GlassDragHandle() },
    ) {
        content()
    }
}

@Composable
private fun GlassDragHandle() {
    Box(
        Modifier.padding(vertical = 10.dp),
        contentAlignment = Alignment.Center,
    ) {
        Box(
            Modifier
                .size(width = 40.dp, height = 5.dp)
                .glassSurface(shape = GlassShapes.pill, tint = Color.White),
        )
    }
}
