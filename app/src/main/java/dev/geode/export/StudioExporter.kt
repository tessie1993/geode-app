package dev.geode.export

import android.content.ContentValues
import android.content.Context
import android.net.Uri
import android.os.Build
import android.provider.MediaStore
import androidx.media3.common.MediaItem
import androidx.media3.common.util.UnstableApi
import androidx.media3.transformer.Composition
import androidx.media3.transformer.EditedMediaItem
import androidx.media3.transformer.Effects
import androidx.media3.transformer.ExportException
import androidx.media3.transformer.ExportResult
import androidx.media3.transformer.ProgressHolder
import androidx.media3.transformer.Transformer
import dev.geode.util.bestEffort
import kotlinx.coroutines.CancellableContinuation
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withContext
import java.io.File
import kotlin.coroutines.resume

@UnstableApi
class StudioExporter(
    private val context: Context,
) {
    sealed interface Result {
        data class Saved(
            val uri: Uri,
            val durationMs: Long,
        ) : Result

        data class Failed(
            val message: String,
        ) : Result

        data object Cancelled : Result
    }

    @Volatile
    private var transformer: Transformer? = null

    @Volatile
    private var cancelled = false

    suspend fun export(
        source: Uri,
        sourceDurationMs: Long,
        edit: ClipEdit,
        displayName: String,
        onProgress: (Float) -> Unit,
    ): Result {
        cancelled = false
        val scratch = File(context.cacheDir, "studio-${System.currentTimeMillis()}.mp4")
        try {
            val outcome =
                withContext(Dispatchers.Main) {
                    runTransformer(source, edit, scratch, sourceDurationMs, onProgress)
                }
            if (outcome != null) return outcome
            if (cancelled) return Result.Cancelled
            val published =
                withContext(Dispatchers.IO) { publish(scratch, displayName) }
            return published
                ?.let { Result.Saved(it, edit.outputMs(sourceDurationMs)) }
                ?: Result.Failed("The finished file could not be saved to Movies/Geode.")
        } finally {
            scratch.delete()
        }
    }

    private suspend fun runTransformer(
        source: Uri,
        edit: ClipEdit,
        output: File,
        sourceDurationMs: Long,
        onProgress: (Float) -> Unit,
    ): Result? =
        suspendCancellableCoroutine { continuation ->
            val item =
                MediaItem
                    .Builder()
                    .setUri(source)
                    .setClippingConfiguration(edit.clipping())
                    .build()
            val edited =
                EditedMediaItem
                    .Builder(item)
                    .setRemoveAudio(edit.mute)
                    .setEffects(Effects(emptyList(), edit.videoEffects()))
                    .apply { edit.speedProvider()?.let { setSpeed(it) } }
                    .build()
            val built =
                Transformer
                    .Builder(context)
                    .addListener(
                        object : Transformer.Listener {
                            override fun onCompleted(
                                composition: Composition,
                                exportResult: ExportResult,
                            ) {
                                transformer = null
                                continuation.resumeOnce(null)
                            }

                            override fun onError(
                                composition: Composition,
                                exportResult: ExportResult,
                                exportException: ExportException,
                            ) {
                                transformer = null
                                continuation.resumeOnce(
                                    if (cancelled) {
                                        Result.Cancelled
                                    } else {
                                        Result.Failed(describe(exportException))
                                    },
                                )
                            }
                        },
                    ).build()
            transformer = built
            continuation.invokeOnCancellation {
                cancelled = true
                bestEffort(TAG, "built.cancel()") { built.cancel() }
            }
            runCatching { built.start(edited, output.absolutePath) }
                .onFailure {
                    transformer = null
                    continuation.resumeOnce(Result.Failed(it.message ?: "The export could not be started."))
                    return@suspendCancellableCoroutine
                }
            val holder = ProgressHolder()
            val scope = kotlinx.coroutines.CoroutineScope(continuation.context)
            scope.launch {
                while (continuation.isActive) {
                    val state = runCatching { built.getProgress(holder) }.getOrNull()
                    if (state == Transformer.PROGRESS_STATE_AVAILABLE) {
                        onProgress(holder.progress / 100f)
                    }
                    delay(PROGRESS_POLL_MS)
                }
            }
            if (sourceDurationMs <= 0L) onProgress(0f)
        }

    fun cancel() {
        cancelled = true
        bestEffort(TAG, "transformer?.cancel()") { transformer?.cancel() }
    }

    private fun publish(
        file: File,
        displayName: String,
    ): Uri? =
        runCatching {
            // Below Q this insert needs WRITE_EXTERNAL_STORAGE, which the app does not hold;
            // failing here rather than mid-insert keeps a half-made row out of the library.
            if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) return null
            val resolver = context.contentResolver
            val values =
                ContentValues().apply {
                    put(MediaStore.Video.Media.DISPLAY_NAME, displayName)
                    put(MediaStore.Video.Media.MIME_TYPE, "video/mp4")
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                        put(MediaStore.Video.Media.RELATIVE_PATH, "Movies/Geode")
                        put(MediaStore.Video.Media.IS_PENDING, 1)
                    }
                }
            val uri =
                resolver.insert(MediaStore.Video.Media.EXTERNAL_CONTENT_URI, values)
                    ?: return null
            resolver.openOutputStream(uri)?.use { out -> file.inputStream().use { it.copyTo(out) } }
                ?: run {
                    resolver.delete(uri, null, null)
                    return null
                }
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                resolver.update(
                    uri,
                    ContentValues().apply { put(MediaStore.Video.Media.IS_PENDING, 0) },
                    null,
                    null,
                )
            }
            uri
        }.getOrNull()

    private fun describe(exception: ExportException): String =
        when (exception.errorCode) {
            ExportException.ERROR_CODE_ENCODER_INIT_FAILED,
            ExportException.ERROR_CODE_ENCODING_FORMAT_UNSUPPORTED,
            ->
                "This device's video encoder would not accept that output — try a smaller size or a " +
                    "different aspect ratio."
            ExportException.ERROR_CODE_DECODER_INIT_FAILED,
            ExportException.ERROR_CODE_DECODING_FORMAT_UNSUPPORTED,
            ExportException.ERROR_CODE_IO_FILE_NOT_FOUND,
            -> "That clip could not be read — the file may have moved, or be in a format this device cannot decode."
            ExportException.ERROR_CODE_IO_NO_PERMISSION -> "Geode does not have permission to read that file."
            else -> exception.message ?: "The export failed."
        }

    private companion object {
        const val PROGRESS_POLL_MS = 250L

        fun CancellableContinuation<Result?>.resumeOnce(value: Result?) {
            if (isActive) resume(value)
        }
    }
}

private const val TAG = "StudioExporter"
