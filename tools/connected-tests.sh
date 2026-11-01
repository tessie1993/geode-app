#!/usr/bin/env bash
# Runs :app:connectedDebugAndroidTest on the CI emulator, keeps evidence, and retries once
# when the failure is the emulator's fault rather than the code's.
#
# Called from android.yml inside reactivecircus/android-emulator-runner, after the APKs were
# built in an earlier step (so Gradle here only installs and runs).
#
# Why a retry at all: the API 30 swiftshader emulator on a 4-vCPU runner loses adb ("device
# 'emulator-5554' not found", "device offline") or stalls the app's main thread for seconds
# while the GL thread renders its first frame in software, which the system answers by killing
# the instrumented process ("Test instrumentation process crashed"). Runs 41178519245,
# 41181213744 and 41190866232 each lost a different journey that way.
#
# What is retried, and how:
#   - adb lost the device, or the run did not complete: the whole suite again, once, after
#     reconnecting adb. Tests that never ran have no result to judge.
#   - Every failure is an instrumentation-process crash: only those tests again, once. A
#     deterministic crash crashes twice and still fails the job.
#   - Anything else (an assertion, a timeout in a waitUntil): no retry. That is a real failure.
# A test that passes only on the retry is reported as a ::warning:: so flakes stay visible.
#
# Evidence goes to ci-artifacts/ (uploaded by the workflow): each attempt's test results and
# reports, and the device's full logcat (main, system, crash and events buffers: the events
# buffer is where am_anr and am_kill say why a process died) plus /data/anr traces.
set -uo pipefail

ART=ci-artifacts
mkdir -p "$ART"
GRADLE=(./gradlew --stacktrace --no-daemon)
RESULTS=app/build/outputs/androidTest-results/connected
INFRA='device .* not found|device offline|AdbCommandRejectedException|ShellCommandUnresponsiveException|Test run failed to complete|DeviceException|no devices/emulators found'

start_logcat() {
    adb logcat -G 16M >/dev/null 2>&1 || true
    adb logcat -b main,system,crash,events -v threadtime >"$ART/logcat-attempt$1.txt" 2>&1 &
    LOGCAT_PID=$!
}

stop_logcat() {
    kill "$LOGCAT_PID" >/dev/null 2>&1 || true
    wait "$LOGCAT_PID" 2>/dev/null || true
}

collect_device_state() {
    local dir="$ART/device-attempt$1"
    mkdir -p "$dir"
    timeout 60 adb root >/dev/null 2>&1 && timeout 30 adb wait-for-device >/dev/null 2>&1
    timeout 120 adb pull /data/anr "$dir/anr" >/dev/null 2>&1 || true
    timeout 120 adb pull /data/tombstones "$dir/tombstones" >/dev/null 2>&1 || true
    timeout 60 adb shell dumpsys meminfo >"$dir/meminfo.txt" 2>&1 || true
    timeout 60 adb shell dumpsys activity exit-info com.muzviz.studiopro.debug >"$dir/exit-info.txt" 2>&1 || true
    timeout 30 adb unroot >/dev/null 2>&1 || true
    timeout 30 adb wait-for-device >/dev/null 2>&1 || true
}

save_attempt() {
    local dir="$ART/attempt$1"
    mkdir -p "$dir"
    cp -r app/build/outputs/androidTest-results "$dir/" 2>/dev/null || true
    cp -r app/build/reports/androidTests "$dir/" 2>/dev/null || true
    cp "$ART/gradle-attempt$1.log" "$dir/" 2>/dev/null || true
}

# Failed test ids (Class#method) from the JUnit XML, and whether every failure was a crash.
failed_tests() {
    python3 - "$RESULTS" "$1" <<'PY'
import glob, sys, xml.etree.ElementTree as ET
root, mode = sys.argv[1], sys.argv[2]
ids, all_crash = [], True
for path in glob.glob(f"{root}/**/TEST-*.xml", recursive=True):
    for case in ET.parse(path).getroot().iter("testcase"):
        bad = case.find("failure") if case.find("failure") is not None else case.find("error")
        if bad is None:
            continue
        ids.append(f"{case.get('classname')}#{case.get('name')}")
        text = (bad.get("message") or "") + (bad.text or "")
        if "instrumentation process crashed" not in text:
            all_crash = False
if mode == "ids":
    print(",".join(ids))
else:
    print("yes" if ids and all_crash else "no")
PY
}

run_attempt() {
    local n=$1
    shift
    start_logcat "$n"
    "${GRADLE[@]}" :app:connectedDebugAndroidTest "$@" 2>&1 | tee "$ART/gradle-attempt$n.log"
    local status=${PIPESTATUS[0]}
    stop_logcat
    [ "$status" -ne 0 ] && collect_device_state "$n"
    save_attempt "$n"
    return "$status"
}

recover_adb() {
    echo "Reconnecting adb before the retry"
    adb reconnect offline >/dev/null 2>&1 || true
    timeout 120 adb wait-for-device || { adb kill-server; adb start-server; timeout 180 adb wait-for-device; }
    timeout 300 bash -c 'until [ "$(adb shell getprop sys.boot_completed 2>/dev/null | tr -d "\r")" = 1 ]; do sleep 2; done'
    adb shell input keyevent 82 >/dev/null 2>&1 || true
}

run_attempt 1 && exit 0

if grep -qE "$INFRA" "$ART/gradle-attempt1.log"; then
    echo "::warning title=Emulator infra failure::adb lost the device or the run did not complete; retrying the whole suite once."
    recover_adb || { echo "::error::The emulator did not come back."; exit 1; }
    run_attempt 2 && {
        echo "::warning title=Passed on retry::The suite passed only after an emulator infra failure. See ci-artifacts/attempt1."
        exit 0
    }
    exit 1
fi

if [ "$(failed_tests crash)" = yes ]; then
    retry=$(failed_tests ids)
    echo "::warning title=Instrumentation crash::Retrying once: $retry"
    recover_adb || exit 1
    run_attempt 2 "-Pandroid.testInstrumentationRunnerArguments.class=$retry" && {
        echo "::warning title=Flaky on the emulator::Crashed, then passed on retry: $retry. Logcat (events buffer: am_anr, am_kill) is in ci-artifacts/."
        exit 0
    }
    exit 1
fi

echo "Real test failures (not an emulator or process crash): no retry."
exit 1
