#pragma once
#include <jni.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <new>
#include <string>
#include <vector>

#include "api/geode_api.h"

namespace geode::jni {

namespace detail {

// java.lang.String, the two methods that code text and the "UTF-8" charset name, looked up once per process and
// held as global references. Null when the lookup failed (out of memory): the next call tries again.
struct StringCalls {
    jclass type;
    jmethodID getBytes;
    jmethodID fromBytes;
    jstring charset;
};

inline const StringCalls* stringCalls(JNIEnv* env) {
    static std::atomic<const StringCalls*> cached{nullptr};
    static std::mutex building;
    if (const StringCalls* calls = cached.load(std::memory_order_acquire)) return calls;
    std::lock_guard<std::mutex> hold(building);
    if (const StringCalls* calls = cached.load(std::memory_order_acquire)) return calls;
    jclass local = env->FindClass("java/lang/String");
    jmethodID getBytes = local ? env->GetMethodID(local, "getBytes", "(Ljava/lang/String;)[B") : nullptr;
    jmethodID fromBytes = getBytes ? env->GetMethodID(local, "<init>", "([BLjava/lang/String;)V") : nullptr;
    jstring name = fromBytes ? env->NewStringUTF("UTF-8") : nullptr;
    auto* type = name ? static_cast<jclass>(env->NewGlobalRef(local)) : nullptr;
    auto* charset = type ? static_cast<jstring>(env->NewGlobalRef(name)) : nullptr;
    env->DeleteLocalRef(name);
    env->DeleteLocalRef(local);
    const StringCalls* calls = charset ? new (std::nothrow) StringCalls{type, getBytes, fromBytes, charset} : nullptr;
    if (!calls) {
        if (charset) env->DeleteGlobalRef(charset);
        if (type) env->DeleteGlobalRef(type);
        return nullptr;
    }
    cached.store(calls, std::memory_order_release);
    return calls;
}

}  // namespace detail

// Text crosses as standard UTF-8, coded by java.lang.String itself. JNI's own UTF calls speak Modified
// UTF-8: GetStringUTFChars hands an emoji in a file name out as two surrogates, which names no file on
// disk, and NewStringUTF aborts under CheckJNI on plain UTF-8 or on a driver log's stray bytes.
class Utf8Chars {
public:
    Utf8Chars(JNIEnv* env, jstring str) {
        if (!str) return;
        const detail::StringCalls* calls = detail::stringCalls(env);
        auto* bytes = calls ? static_cast<jbyteArray>(env->CallObjectMethod(str, calls->getBytes, calls->charset)) : nullptr;
        // With an exception pending (out of memory) only the deletes below are legal: the text stays invalid and
        // the caller's Java frame sees the exception on return.
        if (bytes && !env->ExceptionCheck()) {
            const jsize n = env->GetArrayLength(bytes);
            text_.resize(static_cast<size_t>(n));
            if (n > 0) env->GetByteArrayRegion(bytes, 0, n, reinterpret_cast<jbyte*>(text_.data()));
            valid_ = true;
        }
        env->DeleteLocalRef(bytes);
    }
    Utf8Chars(const Utf8Chars&) = delete;
    Utf8Chars& operator=(const Utf8Chars&) = delete;
    const char* get() const { return valid_ ? text_.c_str() : nullptr; }

private:
    std::string text_;
    bool valid_ = false;
};

// A Java String from standard UTF-8; malformed bytes become U+FFFD. Null when out of memory.
inline jstring utf8String(JNIEnv* env, const char* text) {
    if (!text) return nullptr;
    const auto n = static_cast<jsize>(std::strlen(text));
    jbyteArray bytes = env->NewByteArray(n);
    if (!bytes) return nullptr;
    env->SetByteArrayRegion(bytes, 0, n, reinterpret_cast<const jbyte*>(text));
    const detail::StringCalls* calls = detail::stringCalls(env);
    auto* out = calls ? static_cast<jstring>(env->NewObject(calls->type, calls->fromBytes, bytes, calls->charset)) : nullptr;
    env->DeleteLocalRef(bytes);
    return out;
}

// A Java String[] from a list that crosses the C ABI as standard UTF-8 entries, each followed by '\n'. Null when
// out of memory.
inline jobjectArray utf8Lines(JNIEnv* env, const char* text) {
    std::vector<std::string> entries;
    for (const char* at = text ? text : ""; *at;) {
        const char* end = std::strchr(at, '\n');
        if (!end) end = at + std::strlen(at);
        entries.emplace_back(at, end);
        at = *end ? end + 1 : end;
    }
    const detail::StringCalls* calls = detail::stringCalls(env);
    jobjectArray out = calls ? env->NewObjectArray(static_cast<jsize>(entries.size()), calls->type, nullptr) : nullptr;
    for (size_t i = 0; out && i < entries.size(); ++i) {
        jstring entry = utf8String(env, entries[i].c_str());
        if (!entry) return nullptr;
        env->SetObjectArrayElement(out, static_cast<jsize>(i), entry);
        env->DeleteLocalRef(entry);
    }
    return out;
}

// Read-only view of a Java float[]; released with JNI_ABORT so no copy-back happens.
class FloatElements {
public:
    FloatElements(JNIEnv* env, jfloatArray array)
        : env_(env), array_(array), data_(array ? env->GetFloatArrayElements(array, nullptr) : nullptr),
          length_(array ? env->GetArrayLength(array) : 0) {}
    ~FloatElements() { if (data_) env_->ReleaseFloatArrayElements(array_, data_, JNI_ABORT); }
    FloatElements(const FloatElements&) = delete;
    FloatElements& operator=(const FloatElements&) = delete;
    const jfloat* get() const { return data_; }
    jsize length() const { return length_; }

private:
    JNIEnv* env_;
    jfloatArray array_;
    jfloat* data_;
    jsize length_;
};

// A look from Java (VizNative.vizSetLook, stageSetLook), held for one call. Not ok() when the routes' text
// could not be read: the caller then keeps the last look.
class LookArgs {
public:
    LookArgs(JNIEnv* env, jstring sceneId, jfloatArray params, jstring lfos, jstring adsrs, jfloatArray tuning,
             jstring reactions, jboolean reducedMotion, jstring transitionId, jlong transitionMs)
        : scene_(env, sceneId), transition_(env, transitionId), params_(env, params), lfos_(env, lfos),
          adsrs_(env, adsrs), reactions_(env, reactions), tuning_(env, tuning), ok_(!reactions || reactions_.get()) {
        look_.scene_id = scene_.get();
        look_.params = params_.get();
        look_.param_count = params_.length();
        look_.lfos = lfos_.get();
        look_.adsrs = adsrs_.get();
        look_.tuning = tuning_.get();
        look_.tuning_count = tuning_.length();
        look_.reactions = reactions_.get();
        look_.reduced_motion = reducedMotion == JNI_TRUE ? 1 : 0;
        look_.transition_id = transition_.get();
        look_.transition_ms = transitionMs;
    }
    LookArgs(const LookArgs&) = delete;
    LookArgs& operator=(const LookArgs&) = delete;
    bool ok() const { return ok_; }
    const geode_viz_look* get() const { return &look_; }

private:
    Utf8Chars scene_;
    Utf8Chars transition_;
    FloatElements params_;
    Utf8Chars lfos_;
    Utf8Chars adsrs_;
    Utf8Chars reactions_;
    FloatElements tuning_;
    bool ok_;
    geode_viz_look look_{};
};

}  // namespace geode::jni

// JNI's Modified UTF-8 calls end here: text crosses through Utf8Chars, utf8String and utf8Lines above, and any
// later mention in a file that includes this header is a compile error. Only detail::stringCalls needs
// NewStringUTF, for the ASCII charset name, and it sits above this line. Every JNI file includes this header
// last, after <jni.h>, so no header that declares these names is read after the poison.
#pragma GCC poison GetStringUTFChars ReleaseStringUTFChars NewStringUTF GetStringUTFRegion
