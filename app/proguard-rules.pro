# Geode R8 configuration (release builds only).
#
# The default proguard-android-optimize.txt already keeps annotations, the
# support/AndroidX entry points and — via -keepclasseswithmembernames — any
# class that declares native methods. The rules below cover the things R8
# cannot see from bytecode alone.

# ---------------------------------------------------------------------------
# JNI bridge to libmilkdropjni.so
# ---------------------------------------------------------------------------
# milkdrop_jni.c uses static symbol registration
# (Java_dev_geode_render_scene_MilkdropEngine_nativeCreate, ...), so both the
# class name and the method names have to survive minification exactly.
-keepclasseswithmembernames,includedescriptorclasses class dev.geode.render.scene.MilkdropEngine {
    native <methods>;
}
-keep class dev.geode.render.scene.MilkdropEngine { *; }

# Belt and braces for anything else that grows a native method later.
-keepclasseswithmembernames,includedescriptorclasses class * {
    native <methods>;
}

# ---------------------------------------------------------------------------
# Media3 / ExoPlayer
# ---------------------------------------------------------------------------
# Media3 resolves extractors, decoders and audio-processor implementations by
# name at runtime; it ships consumer rules for its own classes, but the
# reflective renderer lookup also touches platform audiofx wrappers.
-dontwarn androidx.media3.**
-keep class androidx.media3.exoplayer.audio.** { *; }

# ---------------------------------------------------------------------------
# Kotlin / coroutines
# ---------------------------------------------------------------------------
-dontwarn kotlinx.coroutines.**
-keepclassmembers class kotlinx.coroutines.** {
    volatile <fields>;
}

# ---------------------------------------------------------------------------
# JTransforms
# ---------------------------------------------------------------------------
# Pulls in optional Apache Commons Math / large-array helpers that are not on
# the Android classpath; the FFT paths Geode uses do not need them.
-dontwarn org.jtransforms.**
-dontwarn pl.edu.icm.**
-dontwarn org.apache.commons.math3.**

# ---------------------------------------------------------------------------
# Crash reports
# ---------------------------------------------------------------------------
# GeodeApp writes crash-latest.txt for the in-app crash banner. Keeping
# source file + line numbers makes those traces readable while still
# obfuscating names; the mapping file is uploaded to Play alongside the AAB.
-keepattributes SourceFile,LineNumberTable
-renamesourcefileattribute SourceFile

# ---------------------------------------------------------------------------
# SceneParams reflection (the param fade)
# ---------------------------------------------------------------------------
# VisualizerRenderer.LERPED_FLOATS walks SceneParams' declared Float fields
# reflectively and excludes the NOT_FADED entries BY NAME (the UNSET_OVERRIDE
# sentinels and paramFadeSec). With the fields renamed, that exclusion matches
# nothing and the sentinels are lerped through zero - every palette-override
# fade flickers between set and unset, in release builds only, invisible to
# the JVM test suite. Keeping the NAMES (not the members wholesale) costs no
# shrinking: the fields are all live anyway.
-keepclassmembernames class dev.geode.render.scene.SceneParams { <fields>; }
