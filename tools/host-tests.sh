#!/usr/bin/env bash
# Builds and runs the native host tests declared in core/tests/host-tests.txt (one entry per test: its
# profile, its sources, its run arguments). CI's native-host-tests.yml calls this once per group; the
# workflow names no source file, so moving a C++ file edits the manifest and nothing else.
#
#   tools/host-tests.sh [--dry-run] [--only NAME]... [GROUP]...
#
#   GROUP      main (the default), tsan, asan, golden or bench: the `@group` lines of the manifest.
#   --only     build and run just the named test (any group), for a test you are working on.
#   --dry-run  print every command, variables expanded and globs left as written; build and run nothing.
#
# Same compiler as before: the plain `g++` on PATH (CI puts tools/hostcc first, which compiles each source once per flag set).
set -euo pipefail
shopt -s extglob
cd "$(dirname "$0")/.."

MANIFEST=core/tests/host-tests.txt
dry=0
only=()
groups=()
while [ $# -gt 0 ]; do
  case "$1" in
    --dry-run) dry=1 ;;
    --only) only+=("$2"); shift ;;
    -*) echo "host-tests: unknown option $1" >&2; exit 2 ;;
    *) groups+=("$1") ;;
  esac
  shift
done
[ "${#groups[@]}" -gt 0 ] || groups=(main)

declare -A PROFILE
names=() profs=() srcss=() runs=() envs=() grps=() pres=()
group=main
pre=""

# Read the manifest: join continued lines, then file each line by its kind.
acc=""
while IFS= read -r line || [ -n "$line" ]; do
  if [[ "$line" == *\\ ]]; then acc+="${line%\\} "; continue; fi
  line="$acc$line"; acc=""
  [[ "$line" =~ ^[[:space:]]*(#.*)?$ ]] && continue
  if [[ "$line" =~ ^@group[[:space:]]+([A-Za-z0-9_-]+)[[:space:]]*$ ]]; then group="${BASH_REMATCH[1]}"; continue; fi
  if [[ "$line" =~ ^@pre[[:space:]]+([A-Za-z0-9_-]+)[[:space:]]*$ ]]; then pre="${BASH_REMATCH[1]}"; continue; fi
  if [[ "$line" =~ ^profile[[:space:]]+([A-Za-z0-9_-]+)[[:space:]]*=[[:space:]]*(.*)$ ]]; then
    PROFILE["${BASH_REMATCH[1]}"]="${BASH_REMATCH[2]}"; continue
  fi
  if [[ "$line" =~ ^[A-Z_][A-Z0-9_]*= ]]; then eval "$line"; continue; fi
  IFS='|' read -r n p s r e <<< "$line"
  for v in n p s r e; do  # trim each field, without a subshell (this loop reads hundreds of lines)
    t="${!v:-}"; t="${t##+([[:space:]])}"; t="${t%%+([[:space:]])}"; printf -v "$v" '%s' "$t"
  done
  names+=("$n"); profs+=("$p"); srcss+=("$s"); runs+=("$r"); envs+=("$e"); grps+=("$group"); pres+=("$pre"); pre=""
done < "$MANIFEST"

pre_basisu() {
  # Basis builds as upstream does (C++17, NDEBUG, its warnings off), in parallel, the same file list as geode_basisu in core/CMakeLists.txt.
  mkdir -p basisu_obj
  sed -n '/add_library(geode_basisu STATIC/,/zstd.c)/p' core/CMakeLists.txt | grep -oE 'encoder/[A-Za-z0-9_/]+\.cpp|transcoder/[A-Za-z0-9_]+\.cpp' \
    | xargs -P "$(nproc)" -I{} sh -c 'g++ -std=c++17 -O2 -DNDEBUG -w -fno-strict-aliasing '"$BASISU_DEFS"' -DBASISU_SUPPORT_ASTCENC=0 \
        -c "'"$BASISU"'/$0" -o "basisu_obj/$(echo "$0" | tr / _).o"' {}
  gcc -O2 -DNDEBUG -w -c "$BASISU/zstd/zstd.c" -o basisu_obj/zstd.o
  test "$(ls basisu_obj/*.o | wc -l)" -eq 33
}

wanted() {  # is entry $1 selected?
  local i="$1" g
  if [ "${#only[@]}" -gt 0 ]; then
    for g in "${only[@]}"; do [ "$g" = "${names[$i]}" ] && return 0; done
    return 1
  fi
  for g in "${groups[@]}"; do [ "$g" = "${grps[$i]}" ] && return 0; done
  return 1
}

declare -A INDEX
for i in "${!names[@]}"; do INDEX["${names[$i]}"]=$i; done

ran=0
for i in "${!names[@]}"; do
  wanted "$i" || continue
  n="${names[$i]}"
  [ -n "${PROFILE[${profs[$i]}]:-}" ] || { echo "host-tests: $n: no profile ${profs[$i]}" >&2; exit 1; }
  srcs="${srcss[$i]}"
  if [[ "$srcs" == @* ]]; then
    [ -n "${INDEX[${srcs#@}]:-}" ] || { echo "host-tests: $n: no test ${srcs#@}" >&2; exit 1; }
    srcs="${srcss[${INDEX[${srcs#@}]}]}"
  fi
  build="g++ ${PROFILE[${profs[$i]}]} $srcs -o $n"
  if [ "${runs[$i]}" = "-" ]; then run=""; else run="${envs[$i]:+${envs[$i]} }./$n ${runs[$i]}"; fi
  if [ "$dry" = 1 ]; then
    [ -n "${pres[$i]}" ] && echo "PRE ${pres[$i]}"
    # No globbing: the line shows each source as the manifest writes it.
    (set -f; eval "echo BUILD $n: $build"; [ -z "$run" ] || eval "echo RUN $n: $run")
  else
    [ -z "${GITHUB_ACTIONS:-}" ] || echo "::group::$n"
    [ -z "${pres[$i]}" ] || "pre_${pres[$i]}"
    eval "$build" || { echo "::error::host test $n did not build"; exit 1; }
    [ -z "$run" ] || eval "$run" || { echo "::error::host test $n failed"; exit 1; }
    [ -z "${GITHUB_ACTIONS:-}" ] || echo "::endgroup::"
  fi
  ran=$((ran + 1))
done
[ "$ran" -gt 0 ] || { echo "host-tests: nothing selected (${only[*]:-${groups[*]}})" >&2; exit 1; }
echo "host-tests: $ran test(s)"
