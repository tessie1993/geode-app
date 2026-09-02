#!/bin/bash
# Installs the Android SDK pieces this repo's Gradle build needs, idempotently.
#
# Contributors and CI-like containers start here. The build wants:
#   - platforms;android-37.0 (compileSdk 37, app/build.gradle.kts)
#   - build-tools;36.0.0
#   - ndk;28.0.13004108      (ndkVersion; compiles app/src/main/cpp)
#   - cmake;3.22.1           (externalNativeBuild.cmake.version)
#   - platform-tools         (adb, for on-device smoke tests)
# plus a JDK 17+ that is assumed present (the Gradle toolchain does not
# auto-provision one in offline-ish containers).
#
# Usage:
#   tools/setup-android-sdk.sh [sdk-dir]
#
# The SDK lands in $1, else $ANDROID_SDK_DIR, else /home/user/android-sdk.
# A local.properties pointing at it is written next to settings.gradle.kts
# only when none exists - an existing one is the developer's own and is left
# alone. Safe to re-run: every step checks before it acts.
set -euo pipefail

SDK_DIR="${1:-${ANDROID_SDK_DIR:-/home/user/android-sdk}}"
GRADLE_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# Pinned so two runs of this script produce the same toolchain. This is the
# cmdline-tools "latest" build at the time of writing (rev 23.0); bump it
# deliberately, not implicitly.
CMDLINE_TOOLS_ZIP="commandlinetools-linux-16111833_latest.zip"
CMDLINE_TOOLS_URL="https://dl.google.com/android/repository/${CMDLINE_TOOLS_ZIP}"

# sdkmanager has no "platforms;android-37": API 37 is published as the minor-versioned
# "platforms;android-37.0", and only on the preview channel (--channel=3), which is why the
# install below passes that flag. AGP resolves compileSdk = 37 against the android-37.0 directory.
PLATFORM="platforms;android-37.0"
PLATFORM_DIR="android-37.0"
SDK_CHANNEL=3
BUILD_TOOLS="build-tools;36.0.0"
NDK="ndk;28.0.13004108"
CMAKE="cmake;3.22.1"

say() { printf '[android-sdk] %s\n' "$*"; }

# ---- JDK ------------------------------------------------------------------
if ! command -v java >/dev/null 2>&1; then
    say "ERROR: no java on PATH; this build needs JDK 17+" >&2
    exit 1
fi
JAVA_MAJOR="$(java -version 2>&1 | sed -n 's/.*version "\([0-9]*\).*/\1/p' | head -1)"
if [ "${JAVA_MAJOR:-0}" -lt 17 ]; then
    say "ERROR: JDK ${JAVA_MAJOR} found; this build needs 17+" >&2
    exit 1
fi

# ---- cmdline-tools --------------------------------------------------------
SDKMANAGER="$SDK_DIR/cmdline-tools/latest/bin/sdkmanager"
if [ ! -x "$SDKMANAGER" ]; then
    say "installing cmdline-tools into $SDK_DIR"
    mkdir -p "$SDK_DIR/cmdline-tools"
    TMP_ZIP="$(mktemp -t android-cmdline-tools-XXXXXX.zip)"
    trap 'rm -f "$TMP_ZIP"' EXIT
    curl -fsSL -o "$TMP_ZIP" "$CMDLINE_TOOLS_URL"
    unzip -q -o "$TMP_ZIP" -d "$SDK_DIR/cmdline-tools"
    # The zip unpacks as cmdline-tools/; sdkmanager expects .../latest/.
    if [ -d "$SDK_DIR/cmdline-tools/cmdline-tools" ]; then
        rm -rf "$SDK_DIR/cmdline-tools/latest"
        mv "$SDK_DIR/cmdline-tools/cmdline-tools" "$SDK_DIR/cmdline-tools/latest"
    fi
else
    say "cmdline-tools already present"
fi

# ---- licenses + packages --------------------------------------------------
# sdkmanager is idempotent but slow to no-op; the directory checks keep a
# re-run near-instant, which is what lets a session hook call this every time.
NEED=()
[ -d "$SDK_DIR/platform-tools" ] || NEED+=("platform-tools")
[ -d "$SDK_DIR/platforms/$PLATFORM_DIR" ] || NEED+=("$PLATFORM")
[ -d "$SDK_DIR/build-tools/36.0.0" ] || NEED+=("$BUILD_TOOLS")
[ -d "$SDK_DIR/ndk/28.0.13004108" ] || NEED+=("$NDK")
[ -d "$SDK_DIR/cmake/3.22.1" ] || NEED+=("$CMAKE")

if [ "${#NEED[@]}" -gt 0 ]; then
    say "accepting licenses"
    yes | "$SDKMANAGER" --sdk_root="$SDK_DIR" --licenses >/dev/null 2>&1 || true
    say "installing: ${NEED[*]}"
    "$SDKMANAGER" --sdk_root="$SDK_DIR" --channel="$SDK_CHANNEL" "${NEED[@]}"
else
    say "all packages already installed"
fi

# ---- local.properties -----------------------------------------------------
LOCAL_PROPS="$GRADLE_ROOT/local.properties"
if [ ! -f "$LOCAL_PROPS" ]; then
    say "writing $LOCAL_PROPS"
    printf 'sdk.dir=%s\n' "$SDK_DIR" > "$LOCAL_PROPS"
elif ! grep -q "^sdk.dir=" "$LOCAL_PROPS"; then
    say "appending sdk.dir to existing $LOCAL_PROPS"
    printf 'sdk.dir=%s\n' "$SDK_DIR" >> "$LOCAL_PROPS"
else
    say "local.properties already sets sdk.dir; leaving it alone"
fi

say "done: SDK at $SDK_DIR"
