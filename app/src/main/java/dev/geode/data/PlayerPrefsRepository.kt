package dev.geode.data

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

interface PlayerPrefsRepository {
    val prefs: StateFlow<PlayerPrefs>

    suspend fun loaded(): PlayerPrefs

    suspend fun update(transform: (PlayerPrefs) -> PlayerPrefs)
}

class SharedPrefsPlayerPrefsRepository(
    private val store: PlayerPrefsStore,
    scope: CoroutineScope,
) : PlayerPrefsRepository {
    private val _prefs = MutableStateFlow(PlayerPrefs())
    override val prefs: StateFlow<PlayerPrefs> = _prefs.asStateFlow()

    private val firstLoad = CompletableDeferred<PlayerPrefs>()
    private val writeLock = Mutex()

    init {
        scope.launch {
            val loaded = withContext(Dispatchers.IO) { store.load() }
            writeLock.withLock {
                _prefs.value = loaded
                firstLoad.complete(loaded)
            }
        }
    }

    override suspend fun loaded(): PlayerPrefs = firstLoad.await()

    override suspend fun update(transform: (PlayerPrefs) -> PlayerPrefs) {
        firstLoad.await()
        writeLock.withLock {
            val next = transform(_prefs.value).coerced()
            _prefs.value = next
            withContext(Dispatchers.IO) { store.save(next) }
        }
    }
}
