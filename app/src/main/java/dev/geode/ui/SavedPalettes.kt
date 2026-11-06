package dev.geode.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Stable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import dev.geode.data.BuiltInGradients
import dev.geode.data.CustomPalette
import dev.geode.data.PaletteSequence
import dev.geode.data.PaletteStore
import dev.geode.engine.bridge.GeodeNative
import dev.geode.render.scene.SceneParams

/**
 * The palettes the picker offers: the built-in gradients, then the user's, and the palette sequence. Every
 * change is published to the engine's feed (see [PaletteStore.publish]) so the renderers hear it at once.
 * The colour maths is native (core/viz/Palette); this holds lists and writes the stores.
 */
@Stable
internal class SavedPalettes(
    private val store: PaletteStore,
) {
    private val builtIns = BuiltInGradients.ALL.map { withHues(it) }
    private var mine: List<CustomPalette> by mutableStateOf(store.list())

    val items: List<CustomPalette> get() = builtIns + mine
    var sequence: PaletteSequence by mutableStateOf(store.sequence())
        private set

    fun save(palette: CustomPalette): CustomPalette {
        val stored = store.save(withHues(palette))
        mine = store.list()
        store.publish()
        return stored
    }

    fun delete(id: String) {
        store.delete(id)
        mine = store.list()
        updateSequence(sequence.copy(ids = sequence.ids.filterNot { it == id }))
    }

    fun updateSequence(next: PaletteSequence) {
        store.saveSequence(next)
        sequence = next
        store.publish()
    }

    /** Auditions [stops] on the live visual without saving them: a palette under [PaletteStore.TRY_ID]. */
    fun audition(stops: List<Int>): CustomPalette {
        store.publish(mapOf(PaletteStore.TRY_ID to stops.toIntArray()))
        return withHues(CustomPalette(PaletteStore.TRY_ID, PaletteStore.TRY_ID, 0f, 1f, stops))
    }

    /** The hue base and span the native side works out from the stops, for the hue-only families. */
    private fun withHues(palette: CustomPalette): CustomPalette {
        if (palette.stops.isEmpty()) return palette
        val hues = runCatching { GeodeNative.paletteHues(palette.stops.toIntArray()) }.getOrNull()
        return if (hues != null && hues.size == 2) palette.copy(baseHue = hues[0], hueSpan = hues[1]) else palette
    }
}

@Composable
internal fun rememberSavedPalettes(): SavedPalettes {
    val context = LocalContext.current
    return remember(context) { SavedPalettes(PaletteStore(context)) }
}

internal fun paletteChipIndex(
    p: SceneParams,
    saved: List<CustomPalette>,
    second: Boolean,
): Int {
    val custom = if (second) p.usesCustomPalette2 else p.usesCustomPalette
    if (!custom) return if (second) p.palette2 else p.palette
    val id = if (second) p.customPalette2Id else p.customPaletteId
    val index = saved.indexOfFirst { it.id == id }
    return if (index < 0) -1 else SceneParams.PALETTES.size + index
}

internal fun paletteChipSelected(
    p: SceneParams,
    saved: List<CustomPalette>,
    index: Int,
    second: Boolean,
): SceneParams {
    val builtInCount = SceneParams.PALETTES.size
    return if (index < builtInCount) {
        PaletteStore.clear(if (second) p.copy(palette2 = index) else p.copy(palette = index), second)
    } else {
        PaletteStore.applyPalette(p, saved[index - builtInCount], second)
    }
}

/** The colours a slot paints now: its saved palette's stops, else its hue palette sampled. */
internal fun paletteStops(
    p: SceneParams,
    saved: List<CustomPalette>,
    second: Boolean,
): List<Int> {
    val id = if (second) p.customPalette2Id else p.customPaletteId
    val custom = if (second) p.usesCustomPalette2 else p.usesCustomPalette
    val own = saved.firstOrNull { custom && it.id == id }?.stops.orEmpty()
    if (own.isNotEmpty()) return own
    return PaletteStore.legacyStops(
        if (second) p.palette2Base else p.paletteBase,
        if (second) p.palette2Range else p.paletteRange,
    )
}

internal fun swatchColor(rgb: Int): Color = Color(0xFF000000.toInt() or (rgb and 0xFFFFFF))

/** The colours in [stops] as one smooth bar, left to right. */
@Composable
internal fun GradientPreview(
    stops: List<Int>,
    modifier: Modifier = Modifier,
) {
    val colors = stops.map(::swatchColor).let { if (it.size == 1) it + it else it }
    Box(
        modifier =
            modifier
                .height(18.dp)
                .clip(RoundedCornerShape(4.dp))
                .let { if (colors.size >= 2) it.background(Brush.horizontalGradient(colors)) else it },
    )
}
