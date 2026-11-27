#!/usr/bin/env bash
# Fails when Kotlin handles audio data. Owner rule: no audio, analysis or PCM logic in Kotlin;
# samples and buffers never pass through Kotlin; Kotlin sends commands and receives state. Audio is C++ in
# core/ (core/audio, core/sound), behind core/api/geode_api.h.
#
# Scans the Kotlin (and Java) under app/src/main/java and engine/*/src/main for the ways a JVM touches
# samples, one "hit" per file and pattern (comment lines are ignored):
#   audiorecord          AudioRecord (capture reads land in a JVM array)
#   audiotrack           AudioTrack (writes come from a JVM array)
#   codec-buffer         MediaCodec getInputBuffer/getOutputBuffer(s), dequeueInputBuffer/dequeueOutputBuffer,
#                        queueInputBuffer: only in a file that also names audio, so a video-only encoder
#                        (surface input, no sample data) is not a hit
#   extractor-sample     MediaExtractor.readSampleData, same audio-file scope
#   muxer-sample         MediaMuxer.writeSampleData, same audio-file scope
#   media3-audio-type    a class or object that extends or implements Media3's AudioProcessor,
#                        BaseAudioProcessor, AudioProcessorChain, AudioSink, ForwardingAudioSink, AudioOutput,
#                        AudioOutputProvider, ForwardingAudioOutputProvider, Extractor or TeeAudioProcessor.AudioBufferSink
#   media3-audio-new     a Media3 audio processor or sink built here (Tee/Sonic/SilenceSkipping AudioProcessor(),
#                        DefaultAudioSink)
#   visualizer           android.media.audiofx.Visualizer capture: OnDataCaptureListener, setDataCaptureListener,
#                        getWaveForm, getFft
#   external:<fun>       an `external fun` in a Kotlin file whose name has audio, pcm, resampl, aiff or dspprocess (any case)
#                        and that takes or returns a ShortArray, FloatArray, ByteArray, DoubleArray or ByteBuffer
#                        (state arrays of Int and Long are fine: they are not samples)
#   native-call:<fun>    a call to one of those buffer-carrying natives from any Kotlin file
#
# Not seen: a buffer-carrying native whose name does not say audio or pcm, audio handled through a class this
# list does not name (a new Media3 or platform API), a codec-buffer, extractor or muxer use in a file that never
# says "audio", and a Kotlin loop over samples that never touches one of the above.
#
# The gate must pass on main today and only tighten. tools/audio-kotlin-allowlist.txt lists every hit the tree
# still has, one per line:   <permanent|debt> <path> <pattern> <reason>
#   permanent: a platform shell Media3 or Android requires on the JVM (the class must exist; it only forwards).
#   debt: audio handled in Kotlin that moves to core/.
# A hit not on the list fails. A list entry that no longer matches fails too, so an entry is deleted in the PR
# that lands its port and can never silently return. There is no override: port the code to core/ instead.
#
# Usage: tools/audio-kotlin-gate.sh [root]       root defaults to the repository (the list is root/tools/...)
#        tools/audio-kotlin-gate.sh --self-test  a planted tree for every pattern; also run in arch-map.yml
set -uo pipefail

if [ "${1:-}" = "--self-test" ]; then
    self=$(cd "$(dirname "$0")" && pwd)/$(basename "$0")
    t=$(mktemp -d)
    trap 'rm -rf "${t:?}"' EXIT
    J="$t/app/src/main/java/dev/geode"
    E="engine/audio-core/src/main/kotlin/p"
    mkdir -p "$J/audio" "$J/export" "$J/ui" "$t/$E" "$t/tools"
    : > "$t/tools/audio-kotlin-allowlist.txt"
    # Clean: video-only MediaCodec, an image ByteBuffer, comments, a string, a native with no samples.
    printf '%s\n' 'val c = MediaCodec.createEncoderByType("video/avc")' 'val b = codec.getOutputBuffer(i)' \
        'muxer.writeSampleData(track, buf, info)' > "$J/export/VideoOnly.kt"
    printf '%s\n' 'val px = ByteBuffer.allocateDirect(w * h * 4)' '// AudioRecord.read(floats) lives in core' \
        'bestEffort(TAG, "reopen the AudioTrack") { }' 'class Sink : Foo(), AudioThing' \
        'fun f(tap: AudioSink): AudioProcessor.AudioFormat = x' > "$J/ui/Clean.kt"
    printf '%s\n' 'package p' 'object N {' '    external fun pcmRingSize(handle: Long): Int' \
        '    external fun audioState(state: LongArray): Int' \
        '    external fun vizSetLook(handle: Long, params: FloatArray)' '}' > "$t/$E/N.kt"
    bash "$self" "$t" >/dev/null || { echo "::error::audio Kotlin gate self-test: clean tree failed"; exit 1; }
    plant() {
        local f=$1
        shift
        printf '%s\n' "$@" > "$t/$f"
        if bash "$self" "$t" >/dev/null; then echo "::error::audio Kotlin gate self-test: passed: $f: $*"; exit 1; fi
        rm "${t:?}/$f"
    }
    A=app/src/main/java/dev/geode/audio/A.kt
    X=app/src/main/java/dev/geode/export/A.kt
    plant $A 'val r = AudioRecord.Builder().build()'
    plant $A 'val r = android.media.AudioTrack(a, f, 1, 1, 0)'
    plant $X 'val a = MediaFormat.MIMETYPE_AUDIO_AAC' 'val i = codec.dequeueInputBuffer(0)'
    plant $X 'val a = MediaFormat.MIMETYPE_AUDIO_AAC' 'val b = codec.getOutputBuffer(i)'
    plant $X 'val s = "audio"' 'val n = extractor.readSampleData(buf, 0)'
    plant $X 'val s = "audio"' 'muxer.writeSampleData(track, buf, info)'
    plant $A 'class P : BaseAudioProcessor() {'
    plant $A ') : ForwardingAudioSink(sink) {'
    plant $A ') : AudioOutput {'
    plant $A 'val o = object : ForwardingAudioOutputProvider(x) {'
    plant $A 'class T : TeeAudioProcessor.AudioBufferSink {'
    plant $A 'val p = SonicAudioProcessor()'
    plant $A 'v.setDataCaptureListener(l, 1, true, false)'
    plant $A 'val d = v.getFft(bytes)'
    plant $E/M.kt 'object M { external fun pcmThing(a: ShortArray): Int }'
    plant $E/M.kt 'object M {' '  external fun playAudio(' '      handle: Long,' '      buffer: java.nio.ByteBuffer,' '  ): Int' '}'
    plant $E/M.kt 'object M { external fun resamplerRun(a: Long): FloatArray }'
    # A listed hit passes; a call to a listed buffer-carrying native from anywhere else is a new hit.
    printf '%s\n' 'object N2 { external fun pcmWrite(a: FloatArray): Int }' > "$t/$E/N2.kt"
    printf '%s\n' "permanent $E/N2.kt external:pcmWrite the JNI declaration" > "$t/tools/audio-kotlin-allowlist.txt"
    bash "$self" "$t" >/dev/null || { echo "::error::audio Kotlin gate self-test: a listed hit failed"; exit 1; }
    plant app/src/main/java/dev/geode/ui/Caller.kt 'fun f(a: FloatArray) { N2.pcmWrite(a) }'
    # An entry that matches nothing fails; so do a bad kind and an entry with no reason.
    printf '%s\n' "permanent $E/N2.kt external:pcmWrite the JNI declaration" \
        "debt app/src/main/java/dev/geode/ui/Gone.kt audiorecord ported" > "$t/tools/audio-kotlin-allowlist.txt"
    if bash "$self" "$t" >/dev/null; then echo "::error::audio Kotlin gate self-test: a stale entry passed"; exit 1; fi
    printf '%s\n' "maybe $E/N2.kt external:pcmWrite why" > "$t/tools/audio-kotlin-allowlist.txt"
    if bash "$self" "$t" >/dev/null; then echo "::error::audio Kotlin gate self-test: a bad kind passed"; exit 1; fi
    printf '%s\n' "debt $E/N2.kt external:pcmWrite" > "$t/tools/audio-kotlin-allowlist.txt"
    if bash "$self" "$t" >/dev/null; then echo "::error::audio Kotlin gate self-test: an entry with no reason passed"; exit 1; fi
    echo "audio Kotlin gate self-test passed"
    exit 0
fi

root=${1:-.}
list="$root/tools/audio-kotlin-allowlist.txt"
fail=0

SUPER='([A-Za-z]+\.)*(Extractor|BaseAudioProcessor|AudioProcessorChain|AudioProcessor|ForwardingAudioSink|AudioSink|AudioOutputProvider|ForwardingAudioOutputProvider|AudioOutput|AudioBufferSink)\b'
declare -A rx=(
    [audiorecord]='\bAudioRecord\b'
    [audiotrack]='android\.media\.AudioTrack\b|\bAudioTrack(\.|\()'
    [media3-audio-new]='\b(Tee|Sonic|SilenceSkipping)?AudioProcessor[[:space:]]*\(|\bDefaultAudioSink\b'
    [visualizer]='android\.media\.audiofx\.Visualizer\b|\bVisualizer\.OnDataCaptureListener\b|\bsetDataCaptureListener\(|\.(getWaveForm|getFft)\('
)
codec='\b(getInputBuffers?|getOutputBuffers?|dequeueInputBuffer|dequeueOutputBuffer|queueInputBuffer)\('
extract='\breadSampleData\('
mux='\bwriteSampleData\('
# A supertype: after a constructor's ")" or after class/object/interface. A function's return type is no hit.
types='\)[[:space:]]*:[[:space:]]*'"$SUPER"'|\b(class|object|interface)\b[^=(]*:[[:space:]]*'"$SUPER"
sampletype='(ShortArray|FloatArray|ByteArray|DoubleArray|ByteBuffer)'
audioname='(audio|pcm|resampl|aiff|dspprocess)'

# Comment lines and trailing // comments are dropped. Hits are per file and pattern, not per line.
strip() { sed -E '/^[[:space:]]*(\/\/|\/\*|\*)/d; s,[[:space:]]//.*$,,' "$1"; }

hits=$(mktemp)
natives=$(mktemp)
listed=$(mktemp)
files=$(find "$root/app/src/main/java" "$root"/engine/*/src/main \( -name '*.kt' -o -name '*.java' \) -type f 2>/dev/null | sort)

# Buffer-carrying natives: every external fun (signature joined across lines) named for audio or PCM.
for f in $files; do
    strip "$f" | awk -v f="${f#"$root"/}" -v nm="$audioname" -v ty="$sampletype" '
        /(^|[[:space:]])external[[:space:]]+([a-z]+[[:space:]]+)*fun[[:space:]]/ {
            s = $0
            while (s !~ /\)/ && (getline l) > 0) s = s " " l
            if (!match(s, /fun[[:space:]]+(<[^>]*>[[:space:]]*)?[A-Za-z_][A-Za-z0-9_]*/)) next
            name = substr(s, RSTART, RLENGTH)
            sub(/^fun[[:space:]]+(<[^>]*>[[:space:]]*)?/, "", name)
            if (tolower(name) !~ nm) next
            if (s ~ ty) print f " " name
        }'
done | sort -u > "$natives"
while read -r f name; do echo "$f external:$name" >> "$hits"; done < "$natives"
callrx=$(awk '{print $2}' "$natives" | sort -u | paste -sd'|')

for f in $files; do
    rel=${f#"$root"/}
    body=$(strip "$f")
    for id in "${!rx[@]}"; do
        if printf '%s\n' "$body" | grep -qE "${rx[$id]}"; then echo "$rel $id" >> "$hits"; fi
    done
    if printf '%s\n' "$body" | grep -v '\bfun ' | grep -qE "$types"; then echo "$rel media3-audio-type" >> "$hits"; fi
    if printf '%s\n' "$body" | grep -qi 'audio'; then
        if printf '%s\n' "$body" | grep -qE "$codec"; then echo "$rel codec-buffer" >> "$hits"; fi
        if printf '%s\n' "$body" | grep -qE "$extract"; then echo "$rel extractor-sample" >> "$hits"; fi
        if printf '%s\n' "$body" | grep -qE "$mux"; then echo "$rel muxer-sample" >> "$hits"; fi
    fi
    if [ -n "$callrx" ]; then
        printf '%s\n' "$body" | grep -vE 'external[[:space:]].*fun ' | grep -oE "\b($callrx)\(" | sed 's/($//' | sort -u | while read -r n; do
            echo "$rel native-call:$n" >> "$hits"
        done
    fi
done
sort -u "$hits" -o "$hits"

# The list: kind, path, pattern, reason; blank lines and # comments allowed.
if [ -f "$list" ]; then
    n=0
    while IFS= read -r line; do
        n=$((n + 1))
        case "$line" in '' | '#'*) continue ;; esac
        read -r kind path pat reason <<<"$line"
        if { [ "$kind" != permanent ] && [ "$kind" != debt ]; } || [ -z "${pat:-}" ] || [ -z "${reason:-}" ]; then
            echo "::error file=tools/audio-kotlin-allowlist.txt,line=$n,title=Bad allowlist entry::want '<permanent|debt> <path> <pattern> <reason>': $line"
            fail=1
            continue
        fi
        echo "$path $pat" >> "$listed"
    done < "$list"
fi
sort -u "$listed" -o "$listed"

while read -r path pat; do
    echo "::error file=$path,title=Audio in Kotlin::$path handles audio data in Kotlin ($pat). Audio, analysis and PCM are C++ in core/; Kotlin sends commands and receives state. Samples and buffers never pass through the JVM."
    fail=1
done < <(comm -23 "$hits" "$listed")

while read -r path pat; do
    echo "::error file=tools/audio-kotlin-allowlist.txt,title=Stale audio allowlist entry::$path $pat no longer matches: delete its line from tools/audio-kotlin-allowlist.txt (the port landed)."
    fail=1
done < <(comm -13 "$hits" "$listed")

if [ "$fail" = 0 ]; then
    echo "audio Kotlin gate passed ($(wc -l < "$hits") allowlisted hits, each to port or a permanent shell)."
fi
rm -f "${hits:?}" "${natives:?}" "${listed:?}"
exit "$fail"
