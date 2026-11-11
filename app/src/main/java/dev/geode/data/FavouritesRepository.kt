package dev.geode.data

import android.content.SharedPreferences
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext

interface FavouritesRepository {
    val favourites: StateFlow<Set<String>>

    suspend fun toggle(uri: String): Boolean

    suspend fun remove(uri: String)
}

class SharedPrefsFavouritesRepository(
    private val prefs: SharedPreferences,
    private val scope: CoroutineScope,
) : FavouritesRepository {
    private val _favourites = MutableStateFlow<Set<String>>(emptySet())
    override val favourites: StateFlow<Set<String>> = _favourites.asStateFlow()

    private val opened = CompletableDeferred<Unit>()
    private val writeLock = Mutex()

    /** Guarded by [writeLock]; replaced when the file changes under it. */
    private var store: FavouritesStore? = null

    /**
     * The media notification's heart (playback/NotificationButtons) writes the same file through its
     * own [FavouritesStore]. Reloading on every change keeps this copy from saving a stale set over
     * it on the next toggle. The file holds favourites alone, so any change is one. Held in a field:
     * SharedPreferences keeps its listeners weakly.
     */
    private val reloadOnChange =
        SharedPreferences.OnSharedPreferenceChangeListener { _, _ -> scope.launch { reload() } }

    init {
        scope.launch {
            // Registered before the first read, so a write that lands during it is not missed.
            prefs.registerOnSharedPreferenceChangeListener(reloadOnChange)
            reload()
            opened.complete(Unit)
        }
    }

    private suspend fun reload() {
        writeLock.withLock {
            val fresh = withContext(Dispatchers.IO) { FavouritesStore(prefs) }
            store = fresh
            _favourites.value = fresh.all().toSet()
        }
    }

    override suspend fun toggle(uri: String): Boolean {
        opened.await()
        return writeLock.withLock {
            val current = checkNotNull(store)
            val added = withContext(Dispatchers.IO) { current.toggle(uri) }
            _favourites.value = current.all().toSet()
            added
        }
    }

    override suspend fun remove(uri: String) {
        opened.await()
        writeLock.withLock {
            val current = checkNotNull(store)
            withContext(Dispatchers.IO) { current.remove(uri) }
            _favourites.value = current.all().toSet()
        }
    }
}
