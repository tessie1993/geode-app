#include <jni.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "api/geode_api.h"
#include "jni_util.hpp"

namespace {

using TagsHandle = std::unique_ptr<geode_tags, decltype(&geode_tags_destroy)>;

jbyteArray utf8Bytes(JNIEnv* env, const char* text) {
    const std::string_view view(text);
    jbyteArray out = env->NewByteArray(static_cast<jsize>(view.size()));
    if (out && !view.empty()) {
        env->SetByteArrayRegion(out, 0, static_cast<jsize>(view.size()), reinterpret_cast<const jbyte*>(view.data()));
    }
    return out;
}

// One element of a byte[][] as a std::string; a null element (the caller means "leave unchanged") reads
// as std::nullopt rather than "".
std::optional<std::string> stringAt(JNIEnv* env, jobjectArray texts, jsize index) {
    auto bytes = static_cast<jbyteArray>(env->GetObjectArrayElement(texts, index));
    if (!bytes) return std::nullopt;
    const jsize n = env->GetArrayLength(bytes);
    std::string out(static_cast<size_t>(n), '\0');
    if (n > 0) env->GetByteArrayRegion(bytes, 0, n, reinterpret_cast<jbyte*>(out.data()));
    env->DeleteLocalRef(bytes);
    return out;
}

// A long[] as the C ABI's long long, `count` values; a null or short array reads as zeros.
std::vector<long long> longsOf(JNIEnv* env, jlongArray values, jsize count) {
    std::vector<long long> out(static_cast<size_t>(count), 0);
    if (!values) return out;
    const jsize n = std::min(count, env->GetArrayLength(values));
    std::vector<jlong> raw(static_cast<size_t>(n));
    if (n > 0) env->GetLongArrayRegion(values, 0, n, raw.data());
    std::copy(raw.begin(), raw.end(), out.begin());
    return out;
}

geode_library_index* libraryIndexOf(jlong handle) { return reinterpret_cast<geode_library_index*>(handle); }

jintArray intsOf(JNIEnv* env, const std::vector<jint>& values) {
    jintArray out = env->NewIntArray(static_cast<jsize>(values.size()));
    if (out && !values.empty()) env->SetIntArrayRegion(out, 0, static_cast<jsize>(values.size()), values.data());
    return out;
}

}  // namespace

extern "C" {

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_tagsRead(JNIEnv* env, jobject, jint fd, jobjectArray texts, jintArray ints,
                                                  jfloatArray gains) {
    TagsHandle tags(geode_tags_read(fd), &geode_tags_destroy);
    if (!tags || !texts || !ints || !gains) return -1;
    if (env->GetArrayLength(texts) < GEODE_TAG_TEXT_COUNT || env->GetArrayLength(ints) < 4 ||
        env->GetArrayLength(gains) < 4) {
        return -1;
    }
    for (int i = 0; i < GEODE_TAG_TEXT_COUNT; ++i) {
        jbyteArray bytes = utf8Bytes(env, geode_tags_text(tags.get(), static_cast<GeodeTagText>(i)));
        env->SetObjectArrayElement(texts, i, bytes);
        env->DeleteLocalRef(bytes);
    }
    const size_t art = std::min<size_t>(geode_tags_art_bytes(tags.get()), static_cast<size_t>(INT32_MAX));
    const std::array<jint, 4> numbers{geode_tags_year(tags.get()), geode_tags_track(tags.get()),
                                      geode_tags_duration_ms(tags.get()), static_cast<jint>(art)};
    env->SetIntArrayRegion(ints, 0, 4, numbers.data());
    std::array<jfloat, 4> replayGain{};
    const int mask = geode_tags_replaygain(tags.get(), &replayGain[0], &replayGain[1], &replayGain[2], &replayGain[3]);
    env->SetFloatArrayRegion(gains, 0, 4, replayGain.data());
    return mask;
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_tagsWrite(JNIEnv* env, jobject, jint fd, jobjectArray texts, jint year,
                                                   jint track) {
    // A slot beyond the caller's array, or a null element within it, means "leave this field unchanged":
    // both must reach geode_tags_write as a null pointer, not "".
    std::array<std::optional<std::string>, GEODE_TAG_TEXT_COUNT> owned;
    std::array<const char*, GEODE_TAG_TEXT_COUNT> pointers{};
    const jsize count = texts ? env->GetArrayLength(texts) : 0;
    for (int i = 0; i < GEODE_TAG_TEXT_COUNT; ++i) {
        if (i < count) owned[static_cast<size_t>(i)] = stringAt(env, texts, i);
        const std::optional<std::string>& value = owned[static_cast<size_t>(i)];
        pointers[static_cast<size_t>(i)] = value ? value->c_str() : nullptr;
    }
    return geode_tags_write(fd, pointers.data(), year, track) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_librarySearch(JNIEnv* env, jobject, jobjectArray rows, jobjectArray request,
                                                       jintArray out) {
    // rows: 3 UTF-8 byte arrays per track (title, artist, album); request: query, focus, artist, album, title,
    // a null element meaning absent. Only marshalling here; the matching is core/library/Search.
    if (!rows || !request || !out) return 0;
    const jsize cells = env->GetArrayLength(rows);
    std::vector<std::string> owned(static_cast<size_t>(cells));
    std::vector<const char*> pointers(static_cast<size_t>(cells));
    for (jsize i = 0; i < cells; ++i) {
        owned[static_cast<size_t>(i)] = stringAt(env, rows, i).value_or(std::string());
        pointers[static_cast<size_t>(i)] = owned[static_cast<size_t>(i)].c_str();
    }
    std::array<std::optional<std::string>, 5> asked;
    std::array<const char*, 5> askedPointers{};
    const jsize askedCount = env->GetArrayLength(request);
    for (jsize i = 0; i < 5 && i < askedCount; ++i) {
        asked[static_cast<size_t>(i)] = stringAt(env, request, i);
        const std::optional<std::string>& value = asked[static_cast<size_t>(i)];
        askedPointers[static_cast<size_t>(i)] = value ? value->c_str() : nullptr;
    }
    const jsize capacity = env->GetArrayLength(out);
    std::vector<jint> hits(static_cast<size_t>(capacity));
    const int total = geode_library_search(pointers.data(), static_cast<int>(cells / 3), askedPointers[0],
                                           askedPointers[1], askedPointers[2], askedPointers[3], askedPointers[4],
                                           hits.data(), static_cast<int>(capacity));
    const jsize written = std::min<jsize>(capacity, static_cast<jsize>(total));
    if (written > 0) env->SetIntArrayRegion(out, 0, written, hits.data());
    return total;
}

// The Library screen's index (core/library/Search): rows as in librarySearch, a duration and an added time
// per track. Only marshalling here; folding, order and search are native.
JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_libraryIndexCreate(JNIEnv* env, jobject, jobjectArray rows,
                                                            jlongArray durationsMs, jlongArray addedSec) {
    if (!rows) return 0;
    const jsize cells = env->GetArrayLength(rows);
    std::vector<std::string> owned(static_cast<size_t>(cells));
    std::vector<const char*> pointers(static_cast<size_t>(cells));
    for (jsize i = 0; i < cells; ++i) {
        owned[static_cast<size_t>(i)] = stringAt(env, rows, i).value_or(std::string());
        pointers[static_cast<size_t>(i)] = owned[static_cast<size_t>(i)].c_str();
    }
    const jsize count = cells / 3;
    const std::vector<long long> durations = longsOf(env, durationsMs, count);
    const std::vector<long long> added = longsOf(env, addedSec, count);
    return reinterpret_cast<jlong>(
        geode_library_index_create(pointers.data(), durations.data(), added.data(), static_cast<int>(count)));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_libraryIndexDestroy(JNIEnv*, jobject, jlong handle) {
    geode_library_index_destroy(libraryIndexOf(handle));
}

JNIEXPORT jintArray JNICALL
Java_dev_geode_engine_bridge_GeodeNative_libraryIndexOrder(JNIEnv* env, jobject, jlong handle, jint sort) {
    const geode_library_index* index = libraryIndexOf(handle);
    std::vector<jint> rows(static_cast<size_t>(geode_library_index_order(index, sort, nullptr, 0)));
    geode_library_index_order(index, sort, rows.data(), static_cast<int>(rows.size()));
    return intsOf(env, rows);
}

JNIEXPORT jintArray JNICALL
Java_dev_geode_engine_bridge_GeodeNative_libraryIndexSearch(JNIEnv* env, jobject, jlong handle, jbyteArray query) {
    std::string text;
    if (query) {
        const jsize n = env->GetArrayLength(query);
        text.resize(static_cast<size_t>(n));
        if (n > 0) env->GetByteArrayRegion(query, 0, n, reinterpret_cast<jbyte*>(text.data()));
    }
    const geode_library_index* index = libraryIndexOf(handle);
    std::vector<jint> packed(static_cast<size_t>(geode_library_index_search(index, text.c_str(), nullptr, 0)));
    geode_library_index_search(index, text.c_str(), packed.data(), static_cast<int>(packed.size()));
    return intsOf(env, packed);
}

}  // extern "C"
