# R8 keep-rule analysis (2026-10-30)

An r8-analyzer audit of the release R8 configuration (`app/proguard-rules.pro` plus every
consumer rule merged into `:app`), done before the Play release. It found nothing that breaks
the build. Its findings are four rules that keep more than they need to and two that repeat
the AGP defaults. Nothing has been changed yet: each **Action** below is a proposal for a
follow-up PR, which has to be checked on a release build on a device (`docs/DEVICE_CHECKS.md`).

**Source.** `./gradlew :app:analyzeReleaseR8Config` (AGP 9.3.1), run locally at `8588501`.
The task writes `app/build/reports/r8/r8-config-analyzer-release.{pb,html}`. The scores
divide kept items by live items: 18,286 classes, 43,375 fields and 97,835 methods (159,496).
No `-dontoptimize`, `-dontobfuscate` or `-dontshrink` is in effect. Full mode is on, since
`gradle.properties` doesn't set `android.enableR8.fullMode=false`.

## Optimization summary

- **Optimization score**: 97.71% of the code is available for R8 optimizations (e.g. inlining, merging). 2.29% of the codebase can't be optimized by R8.
- **Shrinking score**: 98.01% of the code is open to R8's removal of unused classes, fields and methods. The other 1.99% is kept even where it's unused, so R8 can't remove it.
- **Obfuscation score**: 97.97% of the codebase is available for R8 to obfuscate.

## Keep rules evaluation

### `-keep class androidx.media3.exoplayer.audio.** { *; }` (`proguard-rules.pro:31`)

- **Keeps**: 1,868 items (1.17%). Classes: 103, Fields: 671, Methods: 1,094. The rule is package-wide and blocks shrinking, optimization and obfuscation.
- **Kept items**: `AudioCapabilities$Api29`, `AudioCapabilities$AudioProfile`, `BaseAudioProcessor.configure`, `BaseAudioProcessor.flush`
- **Action**: **Remove**. Media3 1.11.0 ships rules for the renderers it loads by name (`FfmpegAudioRenderer`, `LibopusAudioRenderer`, `LibflacAudioRenderer`, `MpeghAudioRenderer`, `IamfAudioRenderer$Builder`). `TapRenderersFactory`, `MvzAudioProcessorChain` and `PcmTap` use the audio classes directly, without reflection.

### `-keepclassmembernames class dev.geode.render.scene.SceneParams { <fields>; }` (`:60`)

- **Keeps**: 167 fields (0.10%), names only.
- **Kept items**: `paletteBaseOverride`, `paramFadeSec`, and 10 static fields (`CAMERA_PATHS`, `PALETTES`, `DEFAULT`, ...)
- **Action**: **Refine** to `{ !static <fields>; }`. `ParamBlend` and `ParamRandomizer` read only non-static `declaredFields` by name. The rule itself is needed (see its comment).

### `-keep class dev.geode.engine.bridge.GeodeNative { *; }` (`:17`)

- **Keeps**: 95 items (0.06%). Classes: 1, Fields: 1 (`INSTANCE`), Methods: 93 (92 native, plus `<init>`).
- **Kept items**: `GeodeNative.analysisCreate`, `GeodeNative.INSTANCE`
- **Action**: **Remove**. Native code never calls back into Java (`core/` and `app/src/main/cpp` have no `FindClass`, `GetMethodID`, `GetFieldID` or `RegisterNatives`). Only the class name and the native method names have to survive. The AGP default `-keepclasseswithmembernames,includedescriptorclasses class * { native <methods>; }` already keeps those names, and it also lets R8 drop native methods nothing calls.

### `-keepclassmembers class kotlinx.coroutines.** { volatile <fields>; }` (`:37`)

- **Keeps**: 63 fields (0.04%). The rule is package-wide.
- **Kept items**: `_disposer$volatile`, `notCompletedCount$volatile`, `_decisionAndIndex$volatile`
- **Action**: **Remove**. kotlinx-coroutines 1.11.0 bundles R8-specific rules that leave this one out on purpose.

## Subsumed keep rules

### `-keepclasseswithmembernames,includedescriptorclasses class dev.geode.engine.bridge.GeodeNative { native <methods>; }` (`:14`)

- **Subsumed By**: `-keepclasseswithmembernames,includedescriptorclasses class * { native <methods>; }` in AGP's `proguard-android-optimize.txt`
- **Action**: **Remove**.

### `-keepclasseswithmembernames,includedescriptorclasses class * { native <methods>; }` (`:20`)

- **Subsumed By**: the identical rule in AGP's `proguard-android-optimize.txt`. R8 merged the two, so this copy keeps nothing of its own.
- **Action**: **Remove**.
