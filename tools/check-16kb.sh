#!/usr/bin/env bash
# Play's 16 KB page-size requirement (apps with native code that target Android 15+).
#
#   tools/check-16kb.sh <file.apk|file.aab>...
#
# For an APK: `zipalign -c -P 16 -v 4` (uncompressed .so entries start on a 16 KB boundary).
# For an APK or AAB: every packaged .so must have all its LOAD segments aligned to >= 2**14.
# Needs build-tools (zipalign) and the NDK (llvm-readelf) under ANDROID_HOME/ANDROID_SDK_ROOT.
set -euo pipefail
[ $# -gt 0 ] || { echo "usage: $0 <apk|aab>..." >&2; exit 2; }
SDK="${ANDROID_HOME:-${ANDROID_SDK_ROOT:?Android SDK location not set}}"
ZIPALIGN=$(ls "$SDK"/build-tools/*/zipalign 2>/dev/null | sort -V | tail -n 1)
READELF=$(ls "$SDK"/ndk/*/toolchains/llvm/prebuilt/*/bin/llvm-readelf 2>/dev/null | sort -V | tail -n 1)
[ -x "$ZIPALIGN" ] || { echo "::error::zipalign not found under $SDK/build-tools"; exit 2; }
[ -x "$READELF" ] || { echo "::error::llvm-readelf not found under $SDK/ndk"; exit 2; }
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail=0
for f in "$@"; do
  echo "== $f"
  if [[ "$f" == *.apk ]]; then
    "$ZIPALIGN" -c -P 16 -v 4 "$f" | grep -E 'lib/.*\.so|Verification' || true
    "$ZIPALIGN" -c -P 16 4 "$f" || { echo "::error::$f: zipalign -P 16 failed (an .so is not 16 KB aligned or is compressed)"; fail=1; }
  fi
  d="$work/$(basename "$f")"
  mkdir -p "$d"
  unzip -q -o "$f" '*.so' -d "$d" || true
  n=0
  while IFS= read -r so; do
    n=$((n + 1))
    # LOAD program headers: Type VirtAddr PhysAddr FileSiz MemSiz Flg Align
    bad=""
    for a in $("$READELF" -lW "$so" | awk '$1=="LOAD"{print $NF}'); do
      [ $((a)) -ge 16384 ] || bad="$bad $a"
    done
    if [ -n "$bad" ]; then
      echo "::error::${so#$d/}: LOAD segment alignment below 2**14 ($bad)"
      fail=1
    else
      echo "ok  ${so#$d/}"
    fi
  done < <(find "$d" -name '*.so' | sort)
  [ "$n" -gt 0 ] || { echo "::error::$f holds no .so files"; fail=1; }
done
exit $fail
