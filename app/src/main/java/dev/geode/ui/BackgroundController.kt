package dev.geode.ui

import android.app.Application
import android.content.Intent
import android.net.Uri
import dev.geode.RingLog
import dev.geode.data.BackgroundPrefs
import dev.geode.data.BackgroundPrefsStore
import dev.geode.render.UnderlayBlend
import dev.geode.viz.BackgroundImage
import dev.geode.viz.BackgroundPixels
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/** What the live renderer (or an export) should push into `NativeViz.setUnderlay` right now. */
data class BackgroundPushState(
    val pixels: BackgroundPixels?,
    val blend: UnderlayBlend,
    val amount: Float,
)

/**
 * Owns the background image: picking it, persisting the choice, and decoding it (blurred,
 * dimmed, centre-cropped) to whatever pixel size the live surface or an export is currently
 * rendering at.
 *
 * No [Host] here: unlike most controllers this one never calls back into [PlayerSession] — it
 * only reads/writes its own [BackgroundPrefsStore] and publishes [prefs]/[push] for the UI and
 * [dev.geode.ui.EnginePlumbing] to read, so a back-reference would have nothing to carry.
 */
internal class BackgroundController(
    private val application: Application,
    private val storeScope: CoroutineScope,
    private val store: BackgroundPrefsStore,
) {
    private val _prefs = MutableStateFlow(store.load())
    val prefs: StateFlow<BackgroundPrefs> = _prefs

    private val _push = MutableStateFlow(BackgroundPushState(null, _prefs.value.blend, _prefs.value.amount))
    val push: StateFlow<BackgroundPushState> = _push

    // Read/written on scope (Main.immediate) only.
    private var renderWidth = 0
    private var renderHeight = 0
    private var decodeGeneration = 0
    private var redecodeJob: Job? = null

    /** Checks the persisted image is still readable; clears and notes it (no crash) if not. */
    fun start() {
        val uri = _prefs.value.uri ?: return
        storeScope.launch {
            val readable =
                runCatching {
                    application.contentResolver.openInputStream(Uri.parse(uri))?.use { }
                    true
                }.getOrDefault(false)
            if (!readable) {
                RingLog.note(TAG, "persisted background image is no longer readable, clearing")
                withContext(Dispatchers.Main.immediate) { clear() }
            }
        }
    }

    fun pick(uri: Uri) {
        runCatching {
            application.contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
        }
        releasePermission(_prefs.value.uri)
        setPrefs(_prefs.value.copy(uri = uri.toString()))
    }

    fun clear() {
        releasePermission(_prefs.value.uri)
        setPrefs(_prefs.value.copy(uri = null))
    }

    fun setBlend(blend: UnderlayBlend) = setPrefs(_prefs.value.copy(blend = blend), redecode = false)

    fun setAmount(amount: Float) = setPrefs(_prefs.value.copy(amount = amount.coerceIn(0f, 1f)), redecode = false)

    // Debounced: CrystalSlider reports every point of a drag, and a full decode (bitmap decode,
    // crop, scale, blur) is too heavy to redo for each of them - see redecode()'s debounceMs.
    fun setBlurRadius(radius: Int) =
        setPrefs(_prefs.value.copy(blurRadius = radius.coerceIn(BackgroundPrefsStore.BLUR_RANGE)), debounceMs = SLIDER_DEBOUNCE_MS)

    fun setDim(dim: Float) = setPrefs(_prefs.value.copy(dim = dim.coerceIn(0f, 1f)), debounceMs = SLIDER_DEBOUNCE_MS)

    /** The GL surface's (or an offscreen target's) current pixel size; redecodes on a real change. */
    fun setRenderSize(
        width: Int,
        height: Int,
    ) {
        if (width <= 0 || height <= 0) return
        if (width == renderWidth && height == renderHeight) return
        renderWidth = width
        renderHeight = height
        redecode(0L)
    }

    /** Releases the read grant [pick] took, if any - a permission never released is a permission leak. */
    private fun releasePermission(uri: String?) {
        if (uri == null) return
        runCatching {
            application.contentResolver.releasePersistableUriPermission(Uri.parse(uri), Intent.FLAG_GRANT_READ_URI_PERMISSION)
        }
    }

    private fun setPrefs(
        next: BackgroundPrefs,
        redecode: Boolean = true,
        debounceMs: Long = 0L,
    ) {
        _prefs.value = next
        storeScope.launch { store.save(next) }
        if (redecode) {
            redecode(debounceMs)
        } else {
            // Blend/amount are not baked into the decoded pixels (see BackgroundImage), so a
            // change to either only needs a re-push, never a re-decode.
            _push.value = _push.value.copy(blend = next.blend, amount = next.amount)
        }
    }

    private fun redecode(debounceMs: Long) {
        redecodeJob?.cancel()
        val p = _prefs.value
        val uri = p.uri
        val generation = ++decodeGeneration
        if (uri == null || renderWidth <= 0 || renderHeight <= 0) {
            _push.value = BackgroundPushState(null, p.blend, p.amount)
            return
        }
        redecodeJob =
            storeScope.launch {
                // A slider still mid-drag cancels this delay (redecodeJob?.cancel() above) before
                // it ever reaches the decode itself, so a fast drag never queues more than one.
                if (debounceMs > 0) delay(debounceMs)
                val decoded =
                    runCatching {
                        BackgroundImage.decode(application, Uri.parse(uri), renderWidth, renderHeight, p.blurRadius, p.dim)
                    }.getOrNull()
                withContext(Dispatchers.Main.immediate) {
                    // A newer pick/resize/pref change already started its own decode; this one is stale.
                    if (generation != decodeGeneration) return@withContext
                    if (decoded == null) {
                        RingLog.note(TAG, "background image failed to decode, clearing")
                        clear()
                    } else {
                        _push.value = BackgroundPushState(decoded, p.blend, p.amount)
                    }
                }
            }
    }

    private companion object {
        const val TAG = "BackgroundController"

        /** Long enough to swallow a slider drag's whole stream of onValueChange calls as one decode. */
        const val SLIDER_DEBOUNCE_MS = 200L
    }
}
