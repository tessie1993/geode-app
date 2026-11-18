package dev.geode.data

import android.content.Context
import androidx.annotation.WorkerThread
import java.io.File

/**
 * The files a render or a recording is writing into a place the user chose (a document they
 * picked, or a new one in their folder): noted when the write starts, dropped when it ends however
 * it ends. A note still here at the next start belongs to a process that died mid-write, so its file
 * is broken and `GeodeContainer` deletes it. A Videos library row needs no note: it stays
 * `IS_PENDING`, which the pending-row sweep finds.
 *
 * One URI per line in `no_backup/unfinished-outputs`, written before the file is opened for writing.
 */
class UnfinishedOutputs(
    private val file: File,
) {
    constructor(context: Context) : this(File(context.noBackupFilesDir, FILE))

    @WorkerThread
    fun begin(uri: String) = synchronized(LOCK) { write(read() + uri) }

    @WorkerThread
    fun end(uri: String) = synchronized(LOCK) { write(read() - uri) }

    /** What the writes noted and never ended; at start, before anything writes, what a dead process left. */
    @WorkerThread
    fun leftOver(): Set<String> = synchronized(LOCK) { read() }

    private fun read(): Set<String> = runCatching { file.readLines().filter { it.isNotBlank() }.toSet() }.getOrDefault(emptySet())

    private fun write(uris: Set<String>) {
        if (uris.isEmpty()) file.delete() else AtomicWrite.text(file, uris.joinToString("\n"))
    }

    private companion object {
        const val FILE = "unfinished-outputs"

        /** One lock for every instance: the recorder and the session each build their own over the one file. */
        val LOCK = Any()
    }
}
