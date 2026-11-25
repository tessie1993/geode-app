#!/usr/bin/env bash
# Installs a minified APK on the running emulator, starts it cold and fails when it does not
# survive: the launch does not complete, the process dies, or the system records a crash or ANR.
# Evidence goes to ci-artifacts/ (logcat with the crash and events buffers, exit-info, tombstones).
#
# Usage: tools/minified-launch.sh <apk> <application id> <activity class>
set -uo pipefail

APK=$1 PKG=$2 ACTIVITY=$3
ART=ci-artifacts
mkdir -p "$ART"
# How long the started process is watched. The app does not call reportFullyDrawn(), so the
# platform has no "start-up finished" signal to wait for instead; `am start -W` returns at the
# first frame, and the renderer, the player and Cast start after it.
WATCH_SECONDS=30

fail() {
    echo "::error title=Minified build::$1"
    status=1
}

status=0
timeout 300 adb install -r "$APK" || { fail "adb install failed for $APK"; exit 1; }
timeout 30 adb logcat -c || true
adb logcat -b main,system,crash,events -v threadtime >"$ART/logcat.txt" 2>&1 &
LOGCAT_PID=$!

started=$(timeout 300 adb shell am start -W -n "$PKG/$ACTIVITY" 2>&1)
echo "$started"
grep -q '^Status: ok' <<<"$started" || fail "The launch did not complete (am start -W)."
pid=$(timeout 30 adb shell pidof "$PKG" | tr -d '\r')
[ -n "$pid" ] || fail "No process for $PKG after the launch."

# `input` returns once the app has handled the event, so this also proves the main thread is
# answering; a hung one is an ANR in the exit-info below. A modifier key changes nothing on screen.
end=$((SECONDS + WATCH_SECONDS))
while [ "$status" -eq 0 ] && [ "$SECONDS" -lt "$end" ]; do
    timeout 30 adb shell input keyevent KEYCODE_SHIFT_LEFT || fail "The app did not handle an input event."
    now=$(timeout 30 adb shell pidof "$PKG" | tr -d '\r')
    [ "$now" = "$pid" ] || fail "The process died after launch (pid $pid, now '${now:-none}')."
done

# The package was installed fresh, so any exit record is this run's: a crash, a native crash or an ANR.
timeout 60 adb shell dumpsys activity exit-info "$PKG" >"$ART/exit-info.txt" 2>&1 || true
if grep -q 'ApplicationExitInfo #' "$ART/exit-info.txt"; then
    grep -E 'reason=|description=' "$ART/exit-info.txt" || true
    fail "The system recorded an exit of $PKG (ci-artifacts/exit-info.txt)."
fi

kill "$LOGCAT_PID" >/dev/null 2>&1 || true
wait "$LOGCAT_PID" 2>/dev/null || true
if [ "$status" -ne 0 ]; then
    timeout 60 adb logcat -b crash -d -v threadtime | tail -n 80 || true
    timeout 60 adb root >/dev/null 2>&1 && timeout 30 adb wait-for-device >/dev/null 2>&1
    timeout 120 adb pull /data/tombstones "$ART/tombstones" >/dev/null 2>&1 || true
    timeout 120 adb pull /data/anr "$ART/anr" >/dev/null 2>&1 || true
fi
exit "$status"
