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

// Studio's folders (core/library/StudioFiles): only the two paths cross; the move is native.
JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_studioMoveTakesOut(JNIEnv* env, jobject, jstring projectsDir, jstring takesDir) {
    geode::jni::Utf8Chars projects(env, projectsDir);
    geode::jni::Utf8Chars takes(env, takesDir);
    return geode_studio_move_takes_out(projects.get(), takes.get());
}

}  // extern "C"
