package dev.geode.data

import dev.geode.editor.ProjectId
import dev.geode.editor.StudioProject
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/** Takes by name. Each take is a Studio project ([TakeStore]); [projectOf] opens it in Edit. */
interface TakeRepository {
    suspend fun list(): List<TakeInfo>

    suspend fun projectOf(name: String): ProjectId?

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

    /** Where takes render to, or null when the user has not been asked yet. */
    suspend fun saveTarget(): SaveTarget?

    /**
     * Where this take renders: [target], kept as the default for later takes when [remember] is
     * set. False when the target cannot be used, so the caller asks again.
     */
    suspend fun chooseSaveTarget(
        target: SaveTarget,
        remember: Boolean,
    ): Boolean

    /** The take's project with [saveTo] written into its export settings, ready to render. */
    suspend fun projectToRender(
        name: String,
        saveTo: SaveTarget,
    ): StudioProject?
}

class FileTakeRepository(
    private val store: TakeStore,
) : TakeRepository {
    override suspend fun list(): List<TakeInfo> = withContext(Dispatchers.IO) { store.list() }

    override suspend fun projectOf(name: String): ProjectId? = withContext(Dispatchers.IO) { store.projectOf(name) }

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

    override suspend fun saveTarget(): SaveTarget? = withContext(Dispatchers.IO) { store.saveTarget() }

    override suspend fun chooseSaveTarget(
        target: SaveTarget,
        remember: Boolean,
    ): Boolean = withContext(Dispatchers.IO) { store.chooseSaveTarget(target, remember) }

    override suspend fun projectToRender(
        name: String,
        saveTo: SaveTarget,
    ): StudioProject? = withContext(Dispatchers.IO) { store.projectToRender(name, saveTo) }
}
