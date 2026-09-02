#include <jni.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "api/geode_api.h"

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

// One element of a byte[][] as a std::string; a null element reads as "".
std::string stringAt(JNIEnv* env, jobjectArray texts, jsize index) {
    auto bytes = static_cast<jbyteArray>(env->GetObjectArrayElement(texts, index));
    if (!bytes) return {};
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
    std::array<std::string, GEODE_TAG_TEXT_COUNT> owned;
    std::array<const char*, GEODE_TAG_TEXT_COUNT> pointers{};
    const jsize count = texts ? env->GetArrayLength(texts) : 0;
    for (int i = 0; i < GEODE_TAG_TEXT_COUNT; ++i) {
        if (i < count) owned[static_cast<size_t>(i)] = stringAt(env, texts, i);
        pointers[static_cast<size_t>(i)] = owned[static_cast<size_t>(i)].c_str();
    }
    return geode_tags_write(fd, pointers.data(), year, track) ? JNI_TRUE : JNI_FALSE;
}

}  // extern "C"
