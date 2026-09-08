#!/usr/bin/env bash
# hostlink - compiles core/viz and core/util on the HOST and links them into
# one shared object with --no-undefined, against Mesa's real libGLESv2/libEGL.
#
# What this proves: every translation unit the core's CMake lists under
# viz/ and util/ compiles under the project's own flags (-std=c++20
# -fvisibility=hidden -Wall -Wextra) and every symbol they reference is
# defined - by another of them, by GL, by libc, or by one of the named stubs
# below. A changed constructor, a renamed method or a declaration with no
# definition fails here the way it fails in the NDK link, without an SDK, an
# NDK or a device.
#
# What it does not prove: anything about the Android toolchain (clang, the
# NDK's libc++, 16 KB page alignment, LTO), the audio/analysis/library trees
# (they need kissfft, oboe, taglib and mediandk), or that anything runs.
#
# Stubs, all named: android/asset_manager.h and android/log.h are replaced by
# C-linkage declarations, projectM's two CMake-generated headers
# (projectM_export.h, version.h) are written by hand, and every extern "C"
# symbol from those three libraries that the link needs is defined as an
# empty function generated from `nm`. Nothing C++ is stubbed.
#
# projectM's public headers come from the exact commit the submodule pins,
# fetched sparsely (one network round trip, ~2 MB), with the repo's own
# render-fbo backport patch applied to them - the same patch the root
# CMakeLists.txt applies at configure time.
#
#   tools/hostlink/hostlink.sh            # exit 0 on a clean link
#   HOSTLINK_KEEP=1 tools/hostlink/hostlink.sh   # keep the work dir
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
WORK="${HOSTLINK_WORK:-$(mktemp -d /tmp/hostlink-XXXXXX)}"
PM_COMMIT="$(cd "$ROOT" && git ls-tree HEAD third_party/projectm | awk '{print $3}')"
mkdir -p "$WORK/inc/android" "$WORK/inc/projectM-4" "$WORK/obj"
trap '[[ -n "${HOSTLINK_KEEP:-}" ]] || rm -rf "$WORK"' EXIT

# ---- projectM headers, pinned ------------------------------------------------
if [[ ! -d "$WORK/pm" ]]; then
  git clone -q --filter=blob:none --no-checkout https://github.com/projectM-visualizer/projectm.git "$WORK/pm"
  git -C "$WORK/pm" sparse-checkout set src/api/include >/dev/null
  git -C "$WORK/pm" fetch -q --depth 1 origin "$PM_COMMIT"
  git -C "$WORK/pm" checkout -q FETCH_HEAD
  git -C "$WORK/pm" apply --include='src/api/include/*' "$ROOT/tools/projectm-v4.1.7-render-fbo-backport.patch"
fi
cp "$WORK/pm/src/api/include/projectM-4/"*.h "$WORK/inc/projectM-4/"
printf '#pragma once\n#define PROJECTM_EXPORT\n' > "$WORK/inc/projectM-4/projectM_export.h"
cat > "$WORK/inc/projectM-4/version.h" << 'HDR'
#pragma once
#define PROJECTM_VERSION_MAJOR 4
#define PROJECTM_VERSION_MINOR 1
#define PROJECTM_VERSION_PATCH 7
#define PROJECTM_VERSION_STRING "4.1.7"
#define PROJECTM_VERSION_VCS "hostlink"
HDR

# ---- the Android headers the viz tree includes -------------------------------
cat > "$WORK/inc/android/asset_manager.h" << 'HDR'
#pragma once
#include <sys/types.h>
#ifdef __cplusplus
extern "C" {
#endif
struct AAssetManager; struct AAsset;
#define AASSET_MODE_BUFFER 3
AAsset* AAssetManager_open(AAssetManager*, const char*, int);
off_t AAsset_getLength(AAsset*);
int AAsset_read(AAsset*, void*, size_t);
void AAsset_close(AAsset*);
#ifdef __cplusplus
}
#endif
HDR
cat > "$WORK/inc/android/log.h" << 'HDR'
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
#define ANDROID_LOG_DEBUG 3
#define ANDROID_LOG_INFO 4
#define ANDROID_LOG_WARN 5
#define ANDROID_LOG_ERROR 6
int __android_log_print(int, const char*, const char*, ...);
#ifdef __cplusplus
}
#endif
HDR

# ---- compile, with the project's flags ----------------------------------------
mapfile -t SRCS < <(sed -n '/add_library(geode_core STATIC/,/^)/p' "$ROOT/core/CMakeLists.txt" | grep -E '^\s+(viz|util)/' | tr -d ' ')
echo "hostlink: compiling ${#SRCS[@]} translation units"
fail=0
for f in "${SRCS[@]}"; do
  obj="$WORK/obj/$(echo "$f" | tr '/' '_').o"
  if ! g++ -std=c++20 -fvisibility=hidden -Wall -Wextra -fPIC -c -I "$ROOT/core" -I "$WORK/inc" "$ROOT/core/$f" -o "$obj"; then
    echo "hostlink: COMPILE FAILED $f"; fail=1
  fi
done
[[ $fail -eq 0 ]] || exit 1

# ---- stubs for the three libraries that are not here --------------------------
nm "$WORK"/obj/*.o | awk '$1 == "U" {print $2}' | sort -u | grep -E '^(projectm_|AAsset|__android)' > "$WORK/stubs.txt" || true
{ while read -r sym; do echo "void $sym(void) {}"; done < "$WORK/stubs.txt"; } > "$WORK/stubs.c"
gcc -fPIC -c "$WORK/stubs.c" -o "$WORK/obj/zz_stubs.o"
echo "hostlink: $(wc -l < "$WORK/stubs.txt") C symbols stubbed (projectM, AAsset, __android_log_print)"

# ---- link -------------------------------------------------------------------------
g++ -shared -fPIC -Wl,--no-undefined -o "$WORK/libgeode_viz_host.so" "$WORK"/obj/*.o -lGLESv2 -lEGL
echo "hostlink: linked $WORK/libgeode_viz_host.so with no undefined symbols"
