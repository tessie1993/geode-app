# Geode R8 configuration (release builds only).
#
# The default proguard-android-optimize.txt already keeps annotations, the
# support/AndroidX entry points and — via -keepclasseswithmembernames — any
# class that declares native methods. The rules below cover the things R8
# cannot see from bytecode alone.

# ---------------------------------------------------------------------------
# JNI bridge to libgeode.so
# ---------------------------------------------------------------------------
# The geode_*jni.cpp files use static symbol registration
# (Java_dev_geode_engine_bridge_<X>Native_*), so both the class name and the
# method names have to survive minification exactly.
-keepclasseswithmembernames,includedescriptorclasses class dev.geode.engine.bridge.*Native {
    native <methods>;
}
# Anything else that grows a native method later is covered by the default
# file's own -keepclasseswithmembernames rule. No native code calls back into
# Kotlin (no FindClass/GetMethodID in app/src/main/cpp or core), so nothing
# else in the *Native objects needs keeping.

# ---------------------------------------------------------------------------
# Android Frame Pacing (Swappy, androidx.games:games-frame-pacing)
# ---------------------------------------------------------------------------
# Swappy's native code finds these classes by name and binds their natives with
# RegisterNatives; the AAR ships no consumer rules, so these are gamesdk's own
# (games-frame-pacing/extras/lib-proguard-rules.txt).
-keep public class com.google.androidgamesdk.ChoreographerCallback { *; }
-keep public class com.google.androidgamesdk.SwappyDisplayManager { *; }

# ---------------------------------------------------------------------------
# Media3 / ExoPlayer
# ---------------------------------------------------------------------------
# Media3 resolves extractors, decoders and audio-processor implementations by
# name at runtime; it ships consumer rules for its own classes, but the
# reflective renderer lookup also touches platform audiofx wrappers.
# Both rules are wider than they need to be (the dontwarn now also hides
# media3-cast and media3-muxer warnings); narrow them only with a device check
# of playback, since they sit on the audio path.
-dontwarn androidx.media3.**
-keep class androidx.media3.exoplayer.audio.** { *; }

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
# Glance widget
# ---------------------------------------------------------------------------
# Glance builds an ActionCallback (WidgetTransportAction, WidgetStyleAction) from
# its class name with the no-argument constructor.
-keep class * implements androidx.glance.appwidget.action.ActionCallback { public <init>(); }

# ---------------------------------------------------------------------------
# Cast
# ---------------------------------------------------------------------------
# CastContext builds the OptionsProvider named in the manifest from its class name; the Cast
# SDK's own rule keeps its methods, not its constructor.
-keep class dev.geode.cast.CastOptionsProvider { public <init>(); }
