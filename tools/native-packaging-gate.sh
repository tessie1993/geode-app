#!/usr/bin/env bash
# Checks the final APK, not an intermediate .so: both 64-bit ABIs must package libgeode.so,
# GameActivity's direct callback, initializeNativeCode JNI bridge, and exactly one JNI_OnLoad
# must be exported; no unexpected shared library may appear, every ELF LOAD segment must be
# 16 KB aligned, and the APK's
# uncompressed native libraries must pass zipalign's 16 KB check.
#
# Usage: tools/native-packaging-gate.sh [path/to/app-sideload.apk]
set -euo pipefail

apk=${1:-app/build/outputs/apk/sideload/app-sideload.apk}
[[ -f "$apk" ]] || { echo "::error::Native packaging gate: APK not found: $apk"; exit 1; }

sdk_root=${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}
[[ -n "$sdk_root" ]] || { echo "::error::Native packaging gate: ANDROID_HOME/ANDROID_SDK_ROOT is not set"; exit 1; }
zipalign="$sdk_root/build-tools/36.0.0/zipalign"
[[ -x "$zipalign" ]] || { echo "::error::Native packaging gate: zipalign not found: $zipalign"; exit 1; }

python3 - "$apk" <<'PY'
import struct
import sys
import zipfile

apk = sys.argv[1]
abis = {"arm64-v8a", "x86_64"}
allowed_libraries = {"libgeode.so", "liboboe.so", "libc++_shared.so"}
problems = []
geode_abis = set()
seen_entries = set()
checked_libraries = 0


def fail(message):
    problems.append(message)


def unpack(fmt, data, offset, label):
    size = struct.calcsize(fmt)
    if offset < 0 or offset + size > len(data):
        raise ValueError(f"{label}: truncated ELF at offset {offset}")
    return struct.unpack_from(fmt, data, offset)


def elf_load_alignments(data, label):
    if len(data) < 0x40 or data[:4] != b"\x7fELF":
        raise ValueError(f"{label}: not an ELF file")
    if data[4] != 2 or data[5] != 1:
        raise ValueError(f"{label}: expected a little-endian ELF64 file")

    phoff = unpack("<Q", data, 0x20, label)[0]
    phentsize = unpack("<H", data, 0x36, label)[0]
    phnum = unpack("<H", data, 0x38, label)[0]
    if phentsize < 0x38 or phoff + phentsize * phnum > len(data):
        raise ValueError(f"{label}: invalid ELF program-header table")

    alignments = []
    for index in range(phnum):
        at = phoff + index * phentsize
        if unpack("<I", data, at, label)[0] == 1:  # PT_LOAD
            alignments.append(unpack("<Q", data, at + 0x30, label)[0])
    if not alignments:
        raise ValueError(f"{label}: no ELF LOAD segments")
    return alignments


def exported_function_symbols(data, label):
    if len(data) < 0x40 or data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
        raise ValueError(f"{label}: expected a little-endian ELF64 file")

    shoff = unpack("<Q", data, 0x28, label)[0]
    shentsize = unpack("<H", data, 0x3A, label)[0]
    shnum = unpack("<H", data, 0x3C, label)[0]
    if shentsize < 0x40 or shoff + shentsize * shnum > len(data):
        raise ValueError(f"{label}: missing or invalid ELF section-header table")

    sections = []
    for index in range(shnum):
        at = shoff + index * shentsize
        sh_type = unpack("<I", data, at + 0x04, label)[0]
        sh_offset = unpack("<Q", data, at + 0x18, label)[0]
        sh_size = unpack("<Q", data, at + 0x20, label)[0]
        sh_link = unpack("<I", data, at + 0x28, label)[0]
        sh_entsize = unpack("<Q", data, at + 0x38, label)[0]
        if sh_offset + sh_size > len(data):
            raise ValueError(f"{label}: section {index} extends past EOF")
        sections.append((sh_type, sh_offset, sh_size, sh_link, sh_entsize))

    symbols = []
    for sh_type, sh_offset, sh_size, sh_link, sh_entsize in sections:
        if sh_type != 11:  # SHT_DYNSYM
            continue
        if sh_entsize < 0x18 or sh_link >= len(sections):
            raise ValueError(f"{label}: invalid dynamic-symbol table")
        str_type, str_offset, str_size, _, _ = sections[sh_link]
        if str_type != 3:  # SHT_STRTAB
            raise ValueError(f"{label}: dynamic symbols have no string table")
        strings = data[str_offset:str_offset + str_size]
        for at in range(sh_offset, sh_offset + sh_size, sh_entsize):
            st_name = unpack("<I", data, at, label)[0]
            st_info = data[at + 4]
            st_other = data[at + 5]
            st_shndx = unpack("<H", data, at + 6, label)[0]
            if st_name >= len(strings):
                raise ValueError(f"{label}: dynamic symbol has an invalid name offset")
            end = strings.find(b"\0", st_name)
            if end < 0:
                raise ValueError(f"{label}: unterminated dynamic-symbol name")
            name = strings[st_name:end].decode("ascii", errors="replace")
            if st_shndx != 0:  # Defined in this shared object.
                symbols.append((name, st_info >> 4, st_info & 0x0F, st_other & 0x03))
    if not symbols:
        raise ValueError(f"{label}: no defined dynamic symbols")
    return symbols


with zipfile.ZipFile(apk) as archive:
    for entry in archive.infolist():
        if not entry.filename.endswith(".so"):
            continue
        parts = entry.filename.split("/")
        if len(parts) != 3 or parts[0] != "lib":
            fail(f"unexpected native-library path: {entry.filename}")
            continue
        _, abi, library = parts
        label = f"{abi}/{library}"
        if abi not in abis:
            fail(f"{label}: unexpected ABI (expected arm64-v8a and x86_64 only)")
        if library not in allowed_libraries:
            fail(f"{label}: unintended shared library in the APK")
        if (abi, library) in seen_entries:
            fail(f"{label}: duplicate APK entry")
        seen_entries.add((abi, library))
        if entry.compress_type != zipfile.ZIP_STORED:
            fail(f"{label}: shared library is compressed; useLegacyPackaging must stay false")
        data = archive.read(entry)
        try:
            alignments = elf_load_alignments(data, label)
        except ValueError as error:
            fail(str(error))
            continue
        checked_libraries += 1
        bad_alignments = [alignment for alignment in alignments if alignment < 0x4000]
        if bad_alignments:
            fail(f"{label}: LOAD segment alignment below 16 KB: {bad_alignments}")
        print(f"{label}: {len(alignments)} LOAD segments, minimum alignment {min(alignments)}")

        if library != "libgeode.so":
            continue
        geode_abis.add(abi)
        try:
            symbols = exported_function_symbols(data, label)
        except ValueError as error:
            fail(str(error))
            continue
        for required in (
            "GameActivity_onCreate",
            "Java_com_google_androidgamesdk_GameActivity_initializeNativeCode",
            "JNI_OnLoad",
        ):
            definitions = [symbol for symbol in symbols if symbol[0] == required]
            exported = [
                symbol for symbol in definitions
                if symbol[1] == 1  # STB_GLOBAL
                and symbol[2] == 2  # STT_FUNC
                and symbol[3] == 0  # STV_DEFAULT
            ]
            if len(definitions) != 1 or len(exported) != 1:
                fail(
                    f"{label}: expected exactly one exported global/default {required} function; "
                    f"found {len(definitions)} defined and {len(exported)} exported"
                )
            else:
                print(f"{label}: exported {required}")

missing = abis - geode_abis
if missing:
    fail(f"APK is missing libgeode.so for: {', '.join(sorted(missing))}")
if checked_libraries != len(seen_entries):
    fail("native packaging gate could not inspect every packaged shared library")

if problems:
    print("::error title=Native packaging contract broken::The minified APK failed native packaging checks.")
    for problem in problems:
        print(f"::error::{problem}")
    sys.exit(1)

print(
    f"Native ELF/symbol checks passed for {apk}: GameActivity_onCreate, "
    "GameActivity initializeNativeCode, one JNI_OnLoad per ABI, 16 KB LOAD alignment."
)
PY

"$zipalign" -c -P 16 -v 4 "$apk"
echo "Native packaging gate passed: $apk"
