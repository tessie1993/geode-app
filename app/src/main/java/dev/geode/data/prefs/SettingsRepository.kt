package dev.geode.data.prefs

import android.content.SharedPreferences
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext

/**
 * One group of settings as a value the UI and the services collect, and the only way to change it
 * (#1009, PR 3).
 *
 * The shape the player prefs proved: callers never touch the prefs file, never hold a copy they
 * could write back stale, and never read on the main thread. [value] is the defaults until the
 * first load lands, then the saved value; a caller that must not act on defaults suspends on
 * [loaded]. [update] is read-modify-write under one lock, so two writers (Settings, the quick
 * page, a service) each change their own field of the latest value instead of overwriting each
 * other's with a whole object copied earlier.
 */
interface SettingsRepository<T> {
    val value: StateFlow<T>

    /** The saved value, once the file has been read; returns at once after that. */
    suspend fun loaded(): T

    /**
     * For the few callers built synchronously on the main thread before the first load can have
     * landed (a cold start's first screen, the playback session, the screen saver): the loaded
     * value, else the file read on this thread, which blocks as a prefs getter would.
     */
    fun current(): T

    /** Applies [transform] to the latest value, publishes the result and writes it, atomically with any other update. */
    suspend fun update(transform: (T) -> T)
}

/**
 * How one value type is kept in a prefs file: its keys (the schema, `PrefsSchemaTest`), how to read
 * it and how to write it. Pure apart from the prefs calls, so a test can drive it over an in-memory
 * file.
 */
interface PrefsCodec<T> {
    /** What [SettingsRepository.value] holds before the file has been read. */
    val defaults: T

    /** Every key [read] and [write] touch, with its type and backup class. */
    val keys: List<PrefKey>

    fun read(prefs: SharedPreferences): T

    /** Puts every field of [value]; the repository applies the edit. */
    fun write(
        editor: SharedPreferences.Editor,
        value: T,
    )

    /**
     * Puts what changed from [previous] to [value]. By default every field, as [write]; a codec
     * overrides it where a whole write would pin defaults the user never chose, such as a key
     * written on every screen change beside others the user never touched.
     */
    fun writeChanges(
        editor: SharedPreferences.Editor,
        previous: T,
        value: T,
    ) = write(editor, value)

    /** Holds a value to its ranges; applied to every read and every update before it is published. */
    fun normalize(value: T): T = value
}

/**
 * The one [SettingsRepository] over a SharedPreferences file. The file is read once, on [io], in
 * [scope]; every update publishes first, so the UI sees the change at once, then writes on [io]
 * while still holding the lock, so writes reach the file in the order the updates ran.
 */
class PrefsSettingsRepository<T>(
    private val prefs: SharedPreferences,
    private val codec: PrefsCodec<T>,
    scope: CoroutineScope,
    private val io: CoroutineDispatcher = Dispatchers.IO,
) : SettingsRepository<T> {
    private val state = MutableStateFlow(codec.defaults)
    override val value: StateFlow<T> = state.asStateFlow()

    private val firstLoad = CompletableDeferred<T>()
    private val writeLock = Mutex()

    init {
        scope.launch {
            val read = withContext(io) { codec.normalize(codec.read(prefs)) }
            writeLock.withLock {
                state.value = read
                firstLoad.complete(read)
            }
        }
    }

    override suspend fun loaded(): T = firstLoad.await()

    override fun current(): T = if (firstLoad.isCompleted) state.value else codec.normalize(codec.read(prefs))

    override suspend fun update(transform: (T) -> T) {
        firstLoad.await()
        writeLock.withLock {
            val previous = state.value
            val next = codec.normalize(transform(previous))
            state.value = next
            withContext(io) { prefs.edit().also { codec.writeChanges(it, previous, next) }.apply() }
        }
    }
}
