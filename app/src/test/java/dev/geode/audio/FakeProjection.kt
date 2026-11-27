package dev.geode.audio

import android.content.Context
import android.media.projection.MediaProjection
import androidx.test.core.app.ApplicationProvider
import java.lang.reflect.Proxy

/** A real [MediaProjection] over a stand-in for the system's side of it, which keeps the callback it is handed. */
internal class FakeProjection {
    private var systemCallback: Any? = null

    val projection: MediaProjection

    init {
        val iface = Class.forName("android.media.projection.IMediaProjection")
        val impl =
            Proxy.newProxyInstance(iface.classLoader, arrayOf(iface)) { _, method, args ->
                if (method.name == "start") systemCallback = args[0]
                when (method.returnType) {
                    java.lang.Boolean.TYPE -> false
                    Integer.TYPE -> 0
                    else -> null
                }
            }
        projection =
            MediaProjection::class.java
                .getDeclaredConstructor(Context::class.java, iface)
                .newInstance(ApplicationProvider.getApplicationContext<Context>(), impl)
    }

    fun stopBySystem() {
        Class
            .forName("android.media.projection.IMediaProjectionCallback")
            .getMethod("onStop")
            .invoke(checkNotNull(systemCallback))
    }
}
