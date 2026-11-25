---
name: geode-native-core
description: Use when working in Geode's C++20 core (core/, app/src/main/cpp) - the JNI boundary, the geode_api C ABI, the viz renderer and scenes, threading on the GL thread - and to check or test native code on the host.
---

# Geode's native core

`libgeode.so` is built by the root `CMakeLists.txt` plus `core/CMakeLists.txt` (the static libraries `geode_core`, `geode_sound` over `geode_pcm`, and `geode_player`). Every engine internal is here; Kotlin is only the shell.

## The boundary

- `app/src/main/cpp/*_jni.cpp` only marshal values into the `extern "C"` ABI in `core/api/geode_api.h`. No logic in JNI files.
- Every JNI `external fun` lives in the `*Native` object named for its JNI file: `geode_<x>_jni.cpp` declares `<X>Native` (`app/src/main/java/dev/geode/engine/bridge/<X>Native.kt`, in `:app`), `geode_jni.cpp` declares `GeodeNative`, and `geode_viz_jni.cpp`'s are split by name prefix (`VizNative`, `MacroNative`, `ParamsNative`, ...). The JNI symbol is `Java_dev_geode_engine_bridge_<X>Native_<name>`. A new native call changes three places: `geode_api.h`, its `*_jni.cpp`, the object's `<X>Native.kt`. Every object but `GeodeNative` runs `GeodeNative.ensureLoaded()` in its `init`; keep constants `const val` so a JVM test reading one never loads the library.
- Rendering runs on the native stage thread (`core/stage`, one per surface), which owns the EGL context. A new scene is built across frames (`core/viz/gl/ScenePrep.hpp`, `ProgramStager`), never in one, so the main thread never waits out a whole build. Exports (`geode_viz_set_offscreen`) build each scene whole.
- Per frame: `Director` → `NoteResponse` → LFO → ADSR → `Reactions` → `MotionField` → `safety::apply`, then `CameraRig`. Keep new per-frame work allocation-free.

## Check and test on the host

clangd is set up by `compile_flags.txt` at the repo root (C++20, `-Icore`, the test stubs, and `tools/clangd-stubs` for `<android/log.h>`). Run `clangd --check=<file>` for diagnostics. It reads the sources cleanly except these three (`core/library/Tags.cpp`, `core/viz/styles/catalogue/SceneRegistry.cpp`, `core/viz/glsl/ShaderSource.cpp`) need the TagLib submodule or the NDK, and `core/audio/output/OboeOutput.cpp` with `core/api/geode_audio_out_api.cpp` need Oboe's headers (the Prefab AAR Gradle unpacks; everything a host test reaches of the output is in `OutputFifo`).

Unit tests are plain `g++` builds in `core/tests/`; find each one's flags and sources in `core/tests/host-tests.txt` (`bash tools/host-tests.sh --only NAME` builds and runs one) (for example `viz_math_test`, `catalogue_test`, `params_test`). Put new pure logic where such a test can reach it, and write the test before the change. `tools/hostlink` checks the native link on the host.

Never build the app locally: push to a `claude/**` branch and read CI. Submodules (`git submodule update --init --recursive`) are needed only for TagLib work.
