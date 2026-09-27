package dev.geode.export

import android.content.ContentValues
import android.content.Context
import android.net.Uri
import android.os.Build
import android.provider.MediaStore
import androidx.annotation.StringRes
import androidx.media3.common.MediaItem
import androidx.media3.common.util.UnstableApi
import androidx.media3.transformer.Composition
import androidx.media3.transformer.EditedMediaItem
import androidx.media3.transformer.EditedMediaItemSequence
import androidx.media3.transformer.Effects
import androidx.media3.transformer.ExportException
import androidx.media3.transformer.ExportResult
import androidx.media3.transformer.ProgressHolder
import androidx.media3.transformer.Transformer
import dev.geode.R
import dev.geode.RingLog
import dev.geode.util.bestEffort
import kotlinx.coroutines.CancellableContinuation
import kotlinx.coroutines.CompletableDeferred
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
            @StringRes val messageRes: Int? = null,
            val messageArgs: List<Any> = emptyList(),
        ) : Result {
            /** Re-resolves [messageRes] against a live [context], for a UI layer that wants localisation. */
            @Suppress("SpreadOperator")
            fun describe(context: Context): String = messageRes?.let { context.getString(it, *messageArgs.toTypedArray()) } ?: message
        }

        data object Cancelled : Result
    }

    /** Builds a [Result.Failed] whose [Result.Failed.message] is already resolved from [resId]. */
    private fun failed(
        @StringRes resId: Int,
        vararg args: Any,
    ): Result.Failed = Result.Failed(context.getString(resId, *args), resId, args.toList())

    @Volatile
    private var transformer: Transformer? = null

    @Volatile
    private var cancelled = false

    // Completed in exportComposition's finally, once the scratch file is cleaned up and the
    // transformer field is cleared — so a caller that awaits cancel() knows it is safe to start
    // a new export on this instance's single @Volatile transformer field. Pre-completed so that
    // cancel() called with no export in flight returns immediately instead of hanging.
    @Volatile
    private var completion: CompletableDeferred<Unit> = CompletableDeferred(Unit)

    suspend fun export(
        source: Uri,
        sourceDurationMs: Long,
        edit: ClipEdit,
        displayName: String,
        codec: ExportCodec = ExportCodec.H264,
        destination: Uri? = null,
        onProgress: (Float) -> Unit,
    ): Result {
        val lut = edit.lutUri?.let { uri -> withContext(Dispatchers.IO) { CubeLut.load(context, uri) } }
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
                .setEffects(Effects(emptyList(), edit.videoEffects(lut)))
                .apply { edit.speedProvider()?.let { setSpeed(it) } }
                .build()
        val composition = Composition.Builder(EditedMediaItemSequence.Builder().addItem(edited).build()).build()
        return exportComposition(composition, edit.outputMs(sourceDurationMs), displayName, codec, destination, onProgress)
    }

    /**
     * Renders [composition] and saves it either to Movies/Geode (when [destination] is null) or
     * straight into the SAF document [destination] the caller already opened — the same choice
     * [VideoExporter.exportToDestination] offers the visualizer export path. Below API 29
     * [publish] cannot insert into MediaStore at all, so callers on those versions must always
     * pass a [destination]; [ExportHost] enforces that by forcing its folder picker there.
     */
    suspend fun exportComposition(
        composition: Composition,
        outputDurationMs: Long,
        displayName: String,
        codec: ExportCodec = ExportCodec.H264,
        destination: Uri? = null,
        onProgress: (Float) -> Unit,
    ): Result {
        cancelled = false
        completion = CompletableDeferred()
        val scratch = File(context.cacheDir, "studio-${System.currentTimeMillis()}.mp4")
        try {
            val outcome =
                withContext(Dispatchers.Main) {
                    runTransformer(composition, scratch, outputDurationMs, codec.available(), onProgress)
                }
            if (outcome != null) return outcome
            if (cancelled) return Result.Cancelled
            return if (destination != null) {
                withContext(Dispatchers.IO) { publishToDestination(scratch, destination, outputDurationMs) }
            } else {
                val published = withContext(Dispatchers.IO) { publish(scratch, displayName) }
                published
                    ?.let { Result.Saved(it, outputDurationMs) }
                    ?: failed(R.string.export_error_studio_save)
            }
        } finally {
            scratch.delete()
            completion.complete(Unit)
        }
    }

    private suspend fun runTransformer(
        composition: Composition,
        output: File,
        outputDurationMs: Long,
        codec: ExportCodec,
        onProgress: (Float) -> Unit,
    ): Result? =
        suspendCancellableCoroutine { continuation ->
            val built =
                Transformer
                    .Builder(context)
                    .setVideoMimeType(codec.mimeType)
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
                                        describe(exportException)
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
            runCatching { built.start(composition, output.absolutePath) }
                .onFailure {
                    transformer = null
                    RingLog.note(TAG, "transformer.start() failed", it)
                    val resolved = it.message?.let { raw -> Result.Failed(raw) } ?: failed(R.string.export_error_studio_start)
                    continuation.resumeOnce(resolved)
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
            if (outputDurationMs <= 0L) onProgress(0f)
        }

    /**
     * Requests cancellation and suspends until the in-flight export (if any) has actually wound
     * down — the Transformer stopped, the scratch file removed and [transformer] cleared — so a
     * caller only returns to an idle UI, or starts a new export, once this instance is safe to
     * reuse. Returns immediately when nothing is exporting.
     */
    suspend fun cancel() {
        cancelled = true
        // Transformer verifies it is called on the thread it was built on, and it is built inside
        // withContext(Dispatchers.Main) below — while this is called from ExportRun.scope, which is
        // Dispatchers.Default. Cancelling from there threw, the throw was swallowed, and the codec
        // kept encoding into a scratch file the caller had already deleted. Hop to Main, and log a
        // failure rather than hiding it: an API-contract violation is exactly what bestEffort was
        // concealing here.
        withContext(Dispatchers.Main) {
            runCatching { transformer?.cancel() }
                .onFailure { RingLog.note(TAG, "transformer cancel failed: ${it.message}") }
        }
        completion.await()
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

    /**
     * Copies [file] into the SAF document [destination] the caller already created via
     * `CreateDocument`. Mirrors [VideoExporter.exportToDestination]'s write, and cleans up the
     * (now empty or partial) document on any failure rather than leaving a broken file behind.
     */
    private fun publishToDestination(
        file: File,
        destination: Uri,
        outputDurationMs: Long,
    ): Result =
        runCatching {
            val resolver = context.contentResolver
            val wrote =
                resolver.openOutputStream(destination)?.use { out -> file.inputStream().use { it.copyTo(out) } } != null
            if (!wrote) {
                return failed(R.string.export_error_destination_write)
            }
            Result.Saved(destination, outputDurationMs)
        }.getOrElse { e ->
            RingLog.note(TAG, "destination write failed", e)
            failed(R.string.export_error_destination_save)
        }

    /** Turns a Transformer failure into a [Result.Failed] with a resource-backed message. */
    private fun describe(exception: ExportException): Result.Failed {
        RingLog.note(TAG, "transformer export failed (errorCode=${exception.errorCode})", exception)
        return when (exception.errorCode) {
            ExportException.ERROR_CODE_ENCODER_INIT_FAILED,
            ExportException.ERROR_CODE_ENCODING_FORMAT_UNSUPPORTED,
            -> failed(R.string.export_error_encoder_unsupported)
            ExportException.ERROR_CODE_DECODER_INIT_FAILED,
            ExportException.ERROR_CODE_DECODING_FORMAT_UNSUPPORTED,
            ExportException.ERROR_CODE_IO_FILE_NOT_FOUND,
            -> failed(R.string.export_error_decode_unsupported)
            ExportException.ERROR_CODE_IO_NO_PERMISSION -> failed(R.string.export_error_no_permission)
            else -> exception.message?.let { Result.Failed(it) } ?: failed(R.string.export_error_generic)
        }
    }

    private companion object {
        const val PROGRESS_POLL_MS = 250L

        fun CancellableContinuation<Result?>.resumeOnce(value: Result?) {
            if (isActive) resume(value)
        }
    }
}

private const val TAG = "StudioExporter"
