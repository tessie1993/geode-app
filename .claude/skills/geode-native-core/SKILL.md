---
name: geode-native-core
description: Use when working in Geode's C++20 core (core/, app/src/main/cpp) - the JNI boundary, the geode_api C ABI, the viz renderer and scenes, threading on the GL thread - and to check or test native code on the host.
---

# Geode's native core

`libgeode.so` is built by the root `CMakeLists.txt` plus `core/CMakeLists.txt` (`geode_core`). ADR 0003 puts every engine internal here; Kotlin is only the shell.

## The boundary

- `app/src/main/cpp/*_jni.cpp` only marshal values into the `extern "C"` ABI in `core/api/geode_api.h`. No logic in JNI files.
- Every JNI `external fun` lives in `GeodeNative` (`:engine:audio-core`). A new native call changes three places: `geode_api.h`, its `*_jni.cpp`, `GeodeNative`.
- Rendering runs on the `GLSurfaceView` GL thread. A new scene is built across frames (`core/viz/ScenePrep.hpp`, `ProgramStager`), never in one, so the main thread never waits out a whole build. Exports (`geode_viz_set_offscreen`) build each scene whole.
- Per frame: `Director` → `NoteResponse` → LFO → ADSR → `Reactions` → `MotionField` → `safety::apply`, then `CameraRig`. Keep new per-frame work allocation-free.

## Check and test on the host

clangd is set up by `compile_flags.txt` at the repo root (C++20, `-Icore`, the test stubs, and `tools/clangd-stubs` for `<android/log.h>`). Run `clangd --check=<file>` for diagnostics. It reads 87 of the 91 sources cleanly; the other four (`core/library/Tags.cpp`, `core/viz/scenes/MilkdropScene.cpp`, `core/viz/SceneRegistry.cpp`, `core/viz/ShaderSource.cpp`) need the projectM or TagLib submodules or the NDK.

Unit tests are plain `g++` builds in `core/tests/`; copy the exact command for each from `.github/workflows/android.yml` (for example `viz_math_test`, `catalogue_test`, `params_test`). Put new pure logic where such a test can reach it, and write the test before the change. `tools/hostlink` checks the native link on the host.

Never build the app locally: push to a `claude/**` branch and read CI. Submodules (`git submodule update --init --recursive`) are needed only for projectM or TagLib work.
