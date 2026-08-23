package dev.geode.ui

import android.app.Application
import android.net.Uri
import dev.geode.data.MilkTexture
import dev.geode.data.MilkTextureLinks
import dev.geode.data.TextureStore
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

internal class TextureController(
    application: Application,
    private val scope: CoroutineScope,
    private val host: Host,
) {
    interface Host {
        fun onGeneratedPresetsRemoved(paths: List<String>)
    }

    private val store = TextureStore(application)
    private val links = MilkTextureLinks(application)

    private val _textures = MutableStateFlow<List<MilkTexture>>(emptyList())
    val textures: StateFlow<List<MilkTexture>> = _textures

    fun refresh() {
        scope.launch(Dispatchers.IO) {
            val listed = store.list()
            withContext(Dispatchers.Main) { if (_textures.value.isEmpty()) _textures.value = listed }
        }
    }

    fun importTextures(
        uris: List<Uri>,
        onImported: () -> Unit,
    ) {
        if (uris.isEmpty()) return
        scope.launch(Dispatchers.IO) {
            val updated = store.import(uris)
            // A texture that just arrived may be exactly what a broken preset has been waiting
            // for, and a substitution may now have its real match - re-resolve everything.
            links.relinkAll()
            withContext(Dispatchers.Main) {
                _textures.value = updated
                onImported()
            }
        }
    }

    fun removeTexture(name: String) {
        scope.launch(Dispatchers.IO) {
            val outcome = store.removeDetailed(name)
            // A removed texture may have been a link target; rebuilding replaces those links
            // with the next-best resolution instead of leaving them dangling.
            links.relinkAll()
            withContext(Dispatchers.Main) {
                _textures.value = outcome.textures
                val gone = outcome.removedGeneratedPresetPaths
                if (gone.isNotEmpty()) host.onGeneratedPresetsRemoved(gone)
            }
        }
    }

    fun useTexture(
        name: String,
        onReady: (String) -> Unit,
    ) {
        scope.launch(Dispatchers.IO) {
            val path = runCatching { store.generateDisplayPreset(name) }.getOrNull()
            withContext(Dispatchers.Main) { path?.let(onReady) }
        }
    }
}
