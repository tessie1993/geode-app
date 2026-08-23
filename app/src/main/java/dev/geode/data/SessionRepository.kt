package dev.geode.data

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

interface SessionRepository {
    suspend fun load(): SessionStore.Saved?

    suspend fun save(session: SessionStore.Saved): Boolean

    suspend fun clear(): Boolean
}

class FileSessionRepository(
    private val store: SessionStore,
) : SessionRepository {
    override suspend fun load(): SessionStore.Saved? = withContext(Dispatchers.IO) { store.load() }

    override suspend fun save(session: SessionStore.Saved): Boolean = withContext(Dispatchers.IO) { store.save(session) }

    override suspend fun clear(): Boolean = withContext(Dispatchers.IO) { store.clear() }
}
