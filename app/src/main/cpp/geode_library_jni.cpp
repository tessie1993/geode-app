// JNI for the library's documents (core/library through api/geode_api.h): the play history, smart playlists, the
// ordinary playlists and the saved session as text in and text out. Marshals only.
#include <jni.h>

#include <algorithm>
#include <string>
#include <vector>

#include "api/geode_api.h"
#include "jni_util.hpp"

namespace {

using geode::jni::Utf8Chars;
using geode::jni::utf8String;

const char* textOf(const Utf8Chars& chars) { return chars.get() ? chars.get() : ""; }

// A Java String[] as UTF-8 C strings: `pointers` point into `owned`. A null array is none; a null entry reads as "".
struct Texts {
    std::vector<std::string> owned;
    std::vector<const char*> pointers;
    int count() const { return static_cast<int>(pointers.size()); }
};

Texts textsOf(JNIEnv* env, jobjectArray array) {
    Texts out;
    const jsize n = array ? env->GetArrayLength(array) : 0;
    out.owned.reserve(static_cast<size_t>(n));
    for (jsize i = 0; i < n; ++i) {
        auto* item = static_cast<jstring>(env->GetObjectArrayElement(array, i));
        Utf8Chars chars(env, item);
        out.owned.emplace_back(textOf(chars));
        env->DeleteLocalRef(item);
    }
    for (const auto& s : out.owned) out.pointers.push_back(s.c_str());
    return out;
}

// A long[] as the C ABI's long long, `count` values; a null or short array reads as zeros.
std::vector<long long> longsOf(JNIEnv* env, jlongArray values, int count) {
    std::vector<long long> out(static_cast<size_t>(count), 0);
    if (!values) return out;
    const jsize n = std::min(static_cast<jsize>(count), env->GetArrayLength(values));
    std::vector<jlong> raw(static_cast<size_t>(n));
    if (n > 0) env->GetLongArrayRegion(values, 0, n, raw.data());
    for (jsize i = 0; i < n; ++i) out[static_cast<size_t>(i)] = raw[static_cast<size_t>(i)];
    return out;
}

}  // namespace

extern "C" {

JNIEXPORT jboolean JNICALL Java_dev_geode_engine_bridge_LibraryNative_historyCorrupt(JNIEnv* env, jobject, jstring text) {
    Utf8Chars chars(env, text);
    return geode_history_corrupt(textOf(chars)) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL Java_dev_geode_engine_bridge_LibraryNative_historyRecordPlay(JNIEnv* env, jobject, jstring state,
                                                                                      jstring uri, jstring title,
                                                                                      jstring artist, jlong nowMs) {
    Utf8Chars held(env, state);
    Utf8Chars track(env, uri);
    Utf8Chars name(env, title);
    Utf8Chars by(env, artist);
    return utf8String(env, geode_history_record_play(textOf(held), textOf(track), textOf(name), textOf(by), nowMs));
}

JNIEXPORT jstring JNICALL Java_dev_geode_engine_bridge_LibraryNative_historyAddListen(JNIEnv* env, jobject, jstring state,
                                                                                     jstring uri, jlong deltaMs) {
    Utf8Chars held(env, state);
    Utf8Chars track(env, uri);
    return utf8String(env, geode_history_add_listen(textOf(held), textOf(track), deltaMs));
}

JNIEXPORT jstring JNICALL Java_dev_geode_engine_bridge_LibraryNative_historyFileText(JNIEnv* env, jobject, jstring state) {
    Utf8Chars held(env, state);
    return utf8String(env, geode_history_file_text(textOf(held)));
}

JNIEXPORT jstring JNICALL Java_dev_geode_engine_bridge_LibraryNative_historyRecent(JNIEnv* env, jobject, jstring state,
                                                                                  jint limit) {
    Utf8Chars held(env, state);
    return utf8String(env, geode_history_recent(textOf(held), limit));
}

JNIEXPORT jstring JNICALL Java_dev_geode_engine_bridge_LibraryNative_smartList(JNIEnv* env, jobject, jobjectArray texts) {
    const Texts files = textsOf(env, texts);
    return utf8String(env, geode_smart_list(files.pointers.data(), files.count()));
}

JNIEXPORT jstring JNICALL Java_dev_geode_engine_bridge_LibraryNative_smartSaveText(JNIEnv* env, jobject, jstring name,
                                                                                  jboolean matchAll, jint limit,
                                                                                  jobjectArray rules) {
    Utf8Chars title(env, name);
    const Texts cells = textsOf(env, rules);
    return utf8String(env, geode_smart_save_text(textOf(title), matchAll == JNI_TRUE ? 1 : 0, limit,
                                                 cells.pointers.data(), cells.count() / 3));
}

JNIEXPORT jintArray JNICALL Java_dev_geode_engine_bridge_LibraryNative_smartResolve(
    JNIEnv* env, jobject, jboolean matchAll, jint limit, jobjectArray rules, jobjectArray tracks, jlongArray durationsMs,
    jlongArray addedSec, jstring history, jobjectArray favourites, jlong nowMs) {
    const Texts ruleCells = textsOf(env, rules);
    const Texts trackCells = textsOf(env, tracks);
    const Texts favs = textsOf(env, favourites);
    Utf8Chars held(env, history);
    const int count = trackCells.count() / 5;
    const std::vector<long long> durations = longsOf(env, durationsMs, count);
    const std::vector<long long> added = longsOf(env, addedSec, count);
    std::vector<int> rows(static_cast<size_t>(count));
    const int n = geode_smart_resolve(matchAll == JNI_TRUE ? 1 : 0, limit, ruleCells.pointers.data(),
                                      ruleCells.count() / 3, trackCells.pointers.data(), durations.data(),
                                      added.data(), count, textOf(held), favs.pointers.data(), favs.count(), nowMs,
                                      rows.data(), count);
    const jsize kept = static_cast<jsize>(std::min(n, count));
    jintArray out = env->NewIntArray(kept);
    if (out && kept > 0) env->SetIntArrayRegion(out, 0, kept, reinterpret_cast<const jint*>(rows.data()));
    return out;
}

JNIEXPORT jstring JNICALL Java_dev_geode_engine_bridge_LibraryNative_musicPlaylistList(JNIEnv* env, jobject,
                                                                                      jobjectArray texts) {
    const Texts files = textsOf(env, texts);
    return utf8String(env, geode_music_playlist_list(files.pointers.data(), files.count()));
}

JNIEXPORT jstring JNICALL Java_dev_geode_engine_bridge_LibraryNative_musicPlaylistApply(
    JNIEnv* env, jobject, jint kind, jstring name, jstring newName, jstring track, jobjectArray tracks, jint from, jint to,
    jobjectArray texts, jstring named) {
    Utf8Chars playlist(env, name);
    Utf8Chars renamed(env, newName);
    Utf8Chars uri(env, track);
    Utf8Chars namedText(env, named);  // null stays NULL: no file under the name
    const Texts uris = textsOf(env, tracks);
    const Texts files = textsOf(env, texts);
    return utf8String(env, geode_music_playlist_apply(kind, textOf(playlist), textOf(renamed), textOf(uri),
                                                      uris.pointers.data(), uris.count(), from, to,
                                                      files.pointers.data(), files.count(), namedText.get()));
}

JNIEXPORT jstring JNICALL Java_dev_geode_engine_bridge_LibraryNative_musicPlaylistMigration(JNIEnv* env, jobject,
                                                                                           jobjectArray fileNames,
                                                                                           jobjectArray texts) {
    const Texts names = textsOf(env, fileNames);
    const Texts bodies = textsOf(env, texts);
    if (bodies.count() != names.count()) return utf8String(env, "");
    return utf8String(env, geode_music_playlist_migration(names.pointers.data(), bodies.pointers.data(), names.count()));
}

JNIEXPORT jstring JNICALL Java_dev_geode_engine_bridge_LibraryNative_musicPlaylistImportBase(JNIEnv* env, jobject,
                                                                                            jstring parsedName,
                                                                                            jstring fileName,
                                                                                            jstring untitled) {
    Utf8Chars parsed(env, parsedName);
    Utf8Chars file(env, fileName);
    Utf8Chars fallback(env, untitled);
    return utf8String(env, geode_music_playlist_import_base(textOf(parsed), textOf(file), textOf(fallback)));
}

JNIEXPORT jstring JNICALL Java_dev_geode_engine_bridge_LibraryNative_sessionRead(JNIEnv* env, jobject, jstring text,
                                                                               jstring positionText) {
    Utf8Chars chars(env, text);
    Utf8Chars position(env, positionText);
    return utf8String(env, geode_session_read(textOf(chars), textOf(position)));
}

JNIEXPORT jstring JNICALL Java_dev_geode_engine_bridge_LibraryNative_sessionPositionWrite(JNIEnv* env, jobject, jint index,
                                                                                         jlong positionMs) {
    return utf8String(env, geode_session_position_write(index, positionMs));
}

JNIEXPORT jstring JNICALL Java_dev_geode_engine_bridge_LibraryNative_sessionWrite(JNIEnv* env, jobject,
                                                                                 jobjectArray tracks, jint index,
                                                                                 jlong positionMs) {
    const Texts cells = textsOf(env, tracks);
    return utf8String(env, geode_session_write(cells.pointers.data(), cells.count() / 3, index, positionMs));
}

}  // extern "C"
