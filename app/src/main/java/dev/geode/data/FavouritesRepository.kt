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
    scope: CoroutineScope,
) : FavouritesRepository {
    private val _favourites = MutableStateFlow<Set<String>>(emptySet())
    override val favourites: StateFlow<Set<String>> = _favourites.asStateFlow()

    private val store = CompletableDeferred<FavouritesStore>()
    private val writeLock = Mutex()

    init {
        scope.launch {
            val loaded = withContext(Dispatchers.IO) { FavouritesStore(prefs) }
            writeLock.withLock {
                _favourites.value = loaded.all().toSet()
                store.complete(loaded)
            }
        }
    }

    override suspend fun toggle(uri: String): Boolean {
        val opened = store.await()
        return writeLock.withLock {
            val added = withContext(Dispatchers.IO) { opened.toggle(uri) }
            _favourites.value = opened.all().toSet()
            added
        }
    }

    override suspend fun remove(uri: String) {
        val opened = store.await()
        writeLock.withLock {
            withContext(Dispatchers.IO) { opened.remove(uri) }
            _favourites.value = opened.all().toSet()
        }
    }
}
