package dev.geode.ui

import android.content.SharedPreferences
import dev.geode.ui.theme.ThemePack
import dev.geode.ui.theme.ThemePackCatalog
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

data class GeodeUserData(
    val theme: ThemePack,
    val guiPrefs: GuiPrefs,
)

interface UserDataRepository {
    val userData: StateFlow<GeodeUserData>

    val theme: StateFlow<ThemePack>

    val guiPrefs: StateFlow<GuiPrefs>

    val loaded: StateFlow<Boolean>

    suspend fun setTheme(pack: ThemePack)

    suspend fun setGuiPrefs(prefs: GuiPrefs)
}

class SharedPrefsUserDataRepository(
    prefs: SharedPreferences,
    scope: CoroutineScope,
) : UserDataRepository {
    private val store = ThemeStore(prefs)

    private val _theme = MutableStateFlow(ThemePackCatalog.bySlug(null))
    override val theme: StateFlow<ThemePack> = _theme.asStateFlow()

    private val _guiPrefs = MutableStateFlow(GuiPrefs())
    override val guiPrefs: StateFlow<GuiPrefs> = _guiPrefs.asStateFlow()

    private val _loaded = MutableStateFlow(false)
    override val loaded: StateFlow<Boolean> = _loaded.asStateFlow()

    override val userData: StateFlow<GeodeUserData> =
        combine(_theme, _guiPrefs, ::GeodeUserData)
            .stateIn(scope, SharingStarted.Eagerly, GeodeUserData(_theme.value, _guiPrefs.value))

    init {
        scope.launch {
            val pack = withContext(Dispatchers.IO) { store.load() }
            val gui = withContext(Dispatchers.IO) { store.loadGui() }
            if (!_loaded.value) {
                _theme.value = pack
                _guiPrefs.value = gui
                _loaded.value = true
            }
        }
    }

    override suspend fun setTheme(pack: ThemePack) {
        _theme.value = pack
        _loaded.value = true
        withContext(Dispatchers.IO) { store.save(pack) }
    }

    override suspend fun setGuiPrefs(prefs: GuiPrefs) {
        _guiPrefs.value = prefs
        _loaded.value = true
        withContext(Dispatchers.IO) { store.saveGui(prefs) }
    }
}
