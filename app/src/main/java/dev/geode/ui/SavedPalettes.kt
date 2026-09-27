package dev.geode.ui

import androidx.compose.runtime.Composable
import androidx.compose.runtime.Stable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext
import dev.geode.data.CustomPalette
import dev.geode.data.PaletteStore

@Stable
internal class SavedPalettes(
    private val store: PaletteStore,
) {
    var items: List<CustomPalette> by mutableStateOf(store.list())
        private set

    fun save(palette: CustomPalette): CustomPalette {
        val stored = store.save(palette)
        items = store.list()
        return stored
    }

    fun delete(id: String) {
        store.delete(id)
        items = store.list()
    }
}

@Composable
internal fun rememberSavedPalettes(): SavedPalettes {
    val context = LocalContext.current
    return remember(context) { SavedPalettes(PaletteStore(context)) }
}
