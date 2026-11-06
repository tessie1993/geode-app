# Geode R8 configuration (release builds only).
#
# The default proguard-android-optimize.txt already keeps annotations, the
# support/AndroidX entry points and — via -keepclasseswithmembernames — any
# class that declares native methods. The rules below cover the things R8
# cannot see from bytecode alone.

# ---------------------------------------------------------------------------
# JNI bridge to libgeode.so
# ---------------------------------------------------------------------------
# geode_jni.cpp and geode_viz_jni.cpp use static symbol registration
# (Java_dev_geode_engine_bridge_GeodeNative_*), so both the class name and the
# method names have to survive minification exactly.
-keepclasseswithmembernames,includedescriptorclasses class dev.geode.engine.bridge.GeodeNative {
    native <methods>;
}
# Anything else that grows a native method later is covered by the default
# file's own -keepclasseswithmembernames rule. No native code calls back into
# Kotlin (no FindClass/GetMethodID in app/src/main/cpp or core), so nothing
# else in GeodeNative needs keeping.

# ---------------------------------------------------------------------------
# Media3 / ExoPlayer
# ---------------------------------------------------------------------------
# Media3 ships consumer rules for the renderers and extractors it loads by name, and this
# app's audio code (TapRenderersFactory, MvzAudioProcessorChain, PcmTap) calls the audio
# classes directly, so the old package-wide `-keep class androidx.media3.exoplayer.audio.**`
# is gone (1,868 items R8 could not shrink; docs/play/R8_KEEP_RULES.md).
# The dontwarn also hides media3-cast and media3-muxer warnings; narrow it to the classes
# R8 actually reports once a release build's warnings are read.
-dontwarn androidx.media3.**

# ---------------------------------------------------------------------------
# Kotlin / coroutines
# ---------------------------------------------------------------------------
# kotlinx.coroutines ships its own R8 rules (its volatile fields included).
-dontwarn kotlinx.coroutines.**

# ---------------------------------------------------------------------------
# Crash reports
# ---------------------------------------------------------------------------
# GeodeApp writes crash-latest.txt for the in-app crash banner. Keeping
# source file + line numbers makes those traces readable while still
# obfuscating names; the mapping file is uploaded to Play alongside the AAB.
-keepattributes SourceFile,LineNumberTable
-renamesourcefileattribute SourceFile

# ---------------------------------------------------------------------------
# SceneParams reflection (the native wire codec)
# ---------------------------------------------------------------------------
# SceneParamsCodec finds SceneParams' fields BY NAME (declaredFields filtered
# against the native layout's names) to pack and unpack the wire buffer. With
# the fields renamed by R8, no name matches and every param reads its default,
# in release builds only, invisible to the JVM test suite. Keeping the NAMES
# (not the members wholesale) costs no shrinking: the fields are all live. Only
# instance fields: the layout's NAMES never hold a static one.
-keepclassmembernames class dev.geode.render.scene.SceneParams { !static <fields>; }
