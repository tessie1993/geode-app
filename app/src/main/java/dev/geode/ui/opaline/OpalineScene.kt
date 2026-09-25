package dev.geode.ui.opaline

import android.annotation.SuppressLint
import android.net.Uri
import android.os.Handler
import android.os.Looper
import android.view.View
import android.webkit.WebResourceRequest
import android.webkit.WebResourceResponse
import android.webkit.WebView
import android.webkit.WebViewClient
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.Modifier
import androidx.compose.ui.composed
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.PointerEventPass
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.boundsInWindow
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.viewinterop.AndroidView
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import org.json.JSONArray
import org.json.JSONObject
import java.io.ByteArrayInputStream
import java.util.concurrent.atomic.AtomicLong
import android.graphics.Color as AndroidColor

private const val SCENE_ORIGIN = "https://opaline.geode.invalid"
private val nextPartId = AtomicLong()
private val LocalOpaline = staticCompositionLocalOf<OpalineBridge?> { null }

/** Allows native front planes to reveal live material while retaining a readable fallback. */
@Composable
fun opalineReady(): Boolean = LocalOpaline.current?.ready == true

/**
 * One offline WebGL world behind native controls. Native UI retains focus, IME,
 * screen reader semantics, scrolling and all authoritative application state.
 */
@Composable
fun OpalineSceneHost(
    modifier: Modifier = Modifier,
    reducedMotion: Boolean = false,
    section: String = "player",
    active: Boolean = true,
    content: @Composable () -> Unit,
) {
    val bridge = remember { OpalineBridge() }
    val lifecycle = LocalLifecycleOwner.current.lifecycle
    SideEffect {
        bridge.configure(reducedMotion, section, active)
    }
    DisposableEffect(lifecycle, bridge) {
        bridge.setResumed(lifecycle.currentState.isAtLeast(Lifecycle.State.RESUMED))
        val observer =
            LifecycleEventObserver { _, event ->
                when (event) {
                    Lifecycle.Event.ON_RESUME -> bridge.setResumed(true)
                    Lifecycle.Event.ON_PAUSE, Lifecycle.Event.ON_STOP -> bridge.setResumed(false)
                    else -> Unit
                }
            }
        lifecycle.addObserver(observer)
        onDispose {
            lifecycle.removeObserver(observer)
            bridge.dispose()
        }
    }
    Box(
        modifier
            .background(
                Brush.verticalGradient(
                    listOf(Color(0xFF102B3D), Color(0xFF081421), Color(0xFF10242E)),
                ),
            ).onGloballyPositioned { bridge.setViewport(it.boundsInWindow()) },
    ) {
        AndroidView(
            factory = { context -> bridge.createView(context) },
            modifier = Modifier.fillMaxSize(),
            onRelease = { bridge.releaseView(it) },
            update = { bridge.attach(it) },
        )
        CompositionLocalProvider(LocalOpaline provides bridge) {
            content()
        }
    }
}

/**
 * Attaches actual library geometry to this native control's measured bounds.
 * Pointer observation uses the final pass and never consumes native gestures.
 */
fun Modifier.opalinePart(
    element: String = "A01",
    value: Float = 0.5f,
    selected: Boolean = false,
    enabled: Boolean = true,
): Modifier =
    composed {
        val bridge = LocalOpaline.current
        val id = remember { "part-${nextPartId.incrementAndGet()}" }
        val part = remember(id) { OpalinePart(id) }
        SideEffect {
            part.element = element
            part.value = value.takeIf { it.isFinite() }?.coerceIn(0f, 1f) ?: 0.5f
            part.selected = selected
            part.enabled = enabled
            bridge?.register(part)
        }
        DisposableEffect(bridge, id) {
            onDispose { bridge?.unregister(id) }
        }
        this
            .drawBehind {
                if (bridge?.ready != true) {
                    // The semantic surface remains readable during loading or loss of WebGL.
                    drawRoundRect(
                        brush =
                            Brush.verticalGradient(
                                listOf(Color(0xFF38576A), Color(0xFF193345)),
                            ),
                        cornerRadius = CornerRadius(size.minDimension * 0.35f),
                    )
                }
            }.onGloballyPositioned {
                part.bounds = it.boundsInWindow()
                bridge?.register(part)
            }.pointerInput(bridge, id, enabled) {
                if (bridge == null || !enabled) return@pointerInput
                try {
                    awaitPointerEventScope {
                        while (true) {
                            val event = awaitPointerEvent(PointerEventPass.Final)
                            for (change in event.changes) {
                                val action =
                                    when {
                                        change.pressed && !change.previousPressed -> "down"
                                        !change.pressed && change.previousPressed -> "up"
                                        change.pressed -> "move"
                                        else -> null
                                    }
                                if (action != null) {
                                    bridge.touch(
                                        id = id,
                                        pointer = change.id.value,
                                        action = action,
                                        x = change.position.x / size.width.coerceAtLeast(1),
                                        y = change.position.y / size.height.coerceAtLeast(1),
                                    )
                                }
                            }
                        }
                    }
                } finally {
                    bridge.cancel(id)
                }
            }
    }

private class OpalinePart(
    val id: String,
    var element: String = "A01",
    var value: Float = 0.5f,
    var selected: Boolean = false,
    var enabled: Boolean = true,
    var bounds: Rect = Rect.Zero,
) {
    fun json(origin: Offset): JSONObject =
        JSONObject()
            .put("id", id)
            .put("element", element)
            .put("value", value.toDouble())
            .put("selected", selected)
            .put("enabled", enabled)
            .put("x", (bounds.left - origin.x).toDouble())
            .put("y", (bounds.top - origin.y).toDouble())
            .put("width", bounds.width.toDouble())
            .put("height", bounds.height.toDouble())
}

private class OpalineBridge {
    var ready by mutableStateOf(false)
        private set

    private val handler = Handler(Looper.getMainLooper())
    private val parts = linkedMapOf<String, OpalinePart>()
    private val pointers = mutableMapOf<Long, String>()
    private var view: WebView? = null
    private var bounds = Rect.Zero
    private var reducedMotion = false
    private var section = "player"
    private var active = true
    private var resumed = false
    private var disposed = false
    private var posted = false
    private var probes = 0

    private val flush =
        Runnable {
            posted = false
            val target = view
            val hasViewport = bounds.width > 0 && bounds.height > 0
            if (target != null && !disposed && hasViewport) {
                val visibleParts = JSONArray()
                parts.values.filter { it.bounds.overlaps(bounds) }.take(96).forEach {
                    visibleParts.put(it.json(bounds.topLeft))
                }
                val state =
                    JSONObject()
                        .put("width", bounds.width.toDouble())
                        .put("height", bounds.height.toDouble())
                        .put("parts", visibleParts)
                        .put("reducedMotion", reducedMotion)
                        .put("section", section)
                        .put("active", active && resumed)
                target.evaluateJavascript("window.Opaline?.update($state)", null)
            }
        }

    private val probe =
        object : Runnable {
            override fun run() {
                val target = view ?: return
                if (disposed) return
                target.evaluateJavascript("window.Opaline?.status") { result ->
                    if (disposed || target !== view) return@evaluateJavascript
                    ready = result == "\"ready\""
                    if (ready) {
                        target.visibility = View.VISIBLE
                        schedule()
                        if (active && resumed) handler.postDelayed(this, 2_000)
                    } else if (result == "\"failed\"" || result == "\"lost\"") {
                        target.visibility = View.INVISIBLE
                    } else if (probes++ < 30) {
                        handler.postDelayed(this, 250)
                    }
                }
            }
        }

    @SuppressLint("SetJavaScriptEnabled")
    fun createView(context: android.content.Context): WebView =
        WebView(context).apply {
            setBackgroundColor(AndroidColor.TRANSPARENT)
            importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS
            isFocusable = false
            isFocusableInTouchMode = false
            setOnTouchListener { _, _ -> true }
            settings.javaScriptEnabled = true
            settings.allowFileAccess = false
            settings.allowContentAccess = false
            settings.domStorageEnabled = false
            settings.setSupportMultipleWindows(false)
            settings.mediaPlaybackRequiresUserGesture = true
            settings.mixedContentMode = android.webkit.WebSettings.MIXED_CONTENT_NEVER_ALLOW
            webViewClient =
                object : WebViewClient() {
                    override fun shouldOverrideUrlLoading(
                        view: WebView,
                        request: WebResourceRequest,
                    ): Boolean = true

                    override fun shouldInterceptRequest(
                        view: WebView,
                        request: WebResourceRequest,
                    ): WebResourceResponse = localResource(context, request.url)

                    override fun onPageFinished(
                        view: WebView,
                        url: String,
                    ) {
                        if (url == "$SCENE_ORIGIN/index.html") {
                            probes = 0
                            handler.removeCallbacks(probe)
                            handler.post(probe)
                            schedule()
                        }
                    }
                }
            attach(this)
            loadUrl("$SCENE_ORIGIN/index.html")
        }

    fun attach(target: WebView) {
        if (view === target || disposed) return
        view = target
        schedule()
    }

    fun configure(
        reducedMotion: Boolean,
        section: String,
        active: Boolean,
    ) {
        if (this.reducedMotion == reducedMotion && this.section == section && this.active == active) return
        this.reducedMotion = reducedMotion
        this.section = section
        this.active = active
        updateActivity()
        schedule()
    }

    fun setViewport(bounds: Rect) {
        if (this.bounds == bounds) return
        this.bounds = bounds
        schedule()
    }

    fun setResumed(resumed: Boolean) {
        this.resumed = resumed
        updateActivity()
        schedule()
    }

    private fun updateActivity() {
        if (active && resumed) {
            view?.onResume()
            view?.evaluateJavascript("window.Opaline?.resume?.()", null)
            handler.removeCallbacks(probe)
            handler.post(probe)
        } else {
            view?.evaluateJavascript("window.Opaline?.pause()", null)
            view?.onPause()
            pointers.clear()
            handler.removeCallbacks(probe)
        }
    }

    fun register(part: OpalinePart) {
        if (disposed) return
        parts[part.id] = part
        schedule()
    }

    fun unregister(id: String) {
        cancel(id)
        parts.remove(id)
        schedule()
    }

    fun touch(
        id: String,
        pointer: Long,
        action: String,
        x: Float,
        y: Float,
    ) {
        if (disposed || !active || !resumed) return
        if (reducedMotion || !x.isFinite() || !y.isFinite()) return
        if (action == "down") pointers[pointer] = id
        if (action == "up" || action == "cancel") pointers.remove(pointer)
        val event =
            JSONObject()
                .put("id", id)
                .put("pointer", pointer.toString())
                .put("action", action)
                .put("x", x.coerceIn(0f, 1f).toDouble())
                .put("y", y.coerceIn(0f, 1f).toDouble())
        view?.evaluateJavascript("window.Opaline?.touch($event)", null)
    }

    fun cancel(id: String) {
        pointers.filterValues { it == id }.keys.toList().forEach {
            touch(id, it, "cancel", 0f, 0f)
        }
    }

    private fun schedule() {
        if (disposed || posted) return
        posted = true
        handler.postDelayed(flush, 16)
    }

    fun releaseView(target: WebView) {
        if (view === target) view = null
        target.evaluateJavascript("window.Opaline?.dispose()", null)
        target.stopLoading()
        target.destroy()
        ready = false
    }

    fun dispose() {
        if (disposed) return
        view?.evaluateJavascript("window.Opaline?.dispose()", null)
        disposed = true
        handler.removeCallbacksAndMessages(null)
        parts.clear()
        pointers.clear()
    }
}

/** All requests are fulfilled from APK assets or denied; there is no network fallback. */
private fun localResource(
    context: android.content.Context,
    uri: Uri,
): WebResourceResponse {
    val path = uri.path.orEmpty().removePrefix("/")
    val trustedOrigin = uri.scheme == "https" && uri.host == "opaline.geode.invalid"
    val safePath = path.isNotEmpty() && path.split('/').none { it == ".." || it == "." }
    if (!trustedOrigin || !safePath) {
        return WebResourceResponse("text/plain", "UTF-8", 403, "Forbidden", emptyMap(), ByteArrayInputStream(byteArrayOf()))
    }
    val mime =
        when (path.substringAfterLast('.')) {
            "html" -> "text/html"
            "js", "mjs" -> "application/javascript"
            "json" -> "application/json"
            "png" -> "image/png"
            else -> "application/octet-stream"
        }
    return try {
        WebResourceResponse(mime, "UTF-8", context.assets.open("opaline/$path"))
    } catch (_: java.io.IOException) {
        WebResourceResponse("text/plain", "UTF-8", 404, "Not Found", emptyMap(), ByteArrayInputStream(byteArrayOf()))
    }
}
