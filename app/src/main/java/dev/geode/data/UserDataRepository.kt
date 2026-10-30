package dev.geode.data

import android.content.SharedPreferences
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

interface UserDataRepository {
    val guiPrefs: StateFlow<GuiPrefs>

    val loaded: StateFlow<Boolean>

    suspend fun setGuiPrefs(prefs: GuiPrefs)
}

class SharedPrefsUserDataRepository(
    prefs: SharedPreferences,
    scope: CoroutineScope,
) : UserDataRepository {
    private val store = ThemeStore(prefs)

    private val _guiPrefs = MutableStateFlow(GuiPrefs())
    override val guiPrefs: StateFlow<GuiPrefs> = _guiPrefs.asStateFlow()

    private val _loaded = MutableStateFlow(false)
    override val loaded: StateFlow<Boolean> = _loaded.asStateFlow()

    init {
        scope.launch {
            val gui = withContext(Dispatchers.IO) { store.loadGui() }
            if (!_loaded.value) {
                _guiPrefs.value = gui
                _loaded.value = true
            }
        }
    }

    override suspend fun setGuiPrefs(prefs: GuiPrefs) {
        _guiPrefs.value = prefs
        _loaded.value = true
        withContext(Dispatchers.IO) { store.saveGui(prefs) }
    }
}
