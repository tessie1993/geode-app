// JNI for the library's documents (core/library through api/geode_api.h): the play history as text in and text
// out. Marshals only.
#include <jni.h>

#include "api/geode_api.h"
#include "jni_util.hpp"

namespace {

using geode::jni::Utf8Chars;
using geode::jni::utf8String;

const char* textOf(const Utf8Chars& chars) { return chars.get() ? chars.get() : ""; }

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

JNIEXPORT jstring JNICALL Java_dev_geode_engine_bridge_LibraryNative_historyEntry(JNIEnv* env, jobject, jstring state,
                                                                                 jstring uri) {
    Utf8Chars held(env, state);
    Utf8Chars track(env, uri);
    return utf8String(env, geode_history_entry(textOf(held), textOf(track)));
}

}  // extern "C"
