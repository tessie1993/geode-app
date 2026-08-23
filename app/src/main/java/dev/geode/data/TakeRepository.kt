package dev.geode.data

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

interface TakeRepository {
    suspend fun list(): List<TakeInfo>

    suspend fun load(name: String): PerformanceTake.Timeline?

    suspend fun save(
        name: String,
        json: String,
    ): String

    suspend fun delete(name: String)

    suspend fun rename(
        from: String,
        to: String,
    ): Boolean
}

class FileTakeRepository(
    private val store: TakeStore,
) : TakeRepository {
    override suspend fun list(): List<TakeInfo> = withContext(Dispatchers.IO) { store.list() }

    override suspend fun load(name: String): PerformanceTake.Timeline? = withContext(Dispatchers.IO) { store.load(name) }

    override suspend fun save(
        name: String,
        json: String,
    ): String = withContext(Dispatchers.IO) { store.save(name, json) }

    override suspend fun delete(name: String) {
        withContext(Dispatchers.IO) { store.delete(name) }
    }

    override suspend fun rename(
        from: String,
        to: String,
    ): Boolean = withContext(Dispatchers.IO) { store.rename(from, to) }
}
