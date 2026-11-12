#pragma once
#include <jni.h>

#include <cstring>
#include <string>

namespace geode::jni {

// Text crosses as standard UTF-8, coded by java.lang.String itself. JNI's own UTF calls speak Modified
// UTF-8: GetStringUTFChars hands an emoji in a file name out as two surrogates, which names no file on
// disk, and NewStringUTF aborts under CheckJNI on plain UTF-8 or on a driver log's stray bytes.
class Utf8Chars {
public:
    Utf8Chars(JNIEnv* env, jstring str) {
        if (!str) return;
        jclass type = env->GetObjectClass(str);
        jmethodID getBytes = env->GetMethodID(type, "getBytes", "(Ljava/lang/String;)[B");
        jstring charset = getBytes ? env->NewStringUTF("UTF-8") : nullptr;
        auto* bytes = charset ? static_cast<jbyteArray>(env->CallObjectMethod(str, getBytes, charset)) : nullptr;
        // With an exception pending (out of memory) only the deletes below are legal: the text stays invalid and
        // the caller's Java frame sees the exception on return.
        if (bytes && !env->ExceptionCheck()) {
            const jsize n = env->GetArrayLength(bytes);
            text_.resize(static_cast<size_t>(n));
            if (n > 0) env->GetByteArrayRegion(bytes, 0, n, reinterpret_cast<jbyte*>(text_.data()));
            valid_ = true;
        }
        env->DeleteLocalRef(bytes);
        env->DeleteLocalRef(charset);
        env->DeleteLocalRef(type);
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
    jclass type = env->FindClass("java/lang/String");
    jmethodID fromBytes = type ? env->GetMethodID(type, "<init>", "([BLjava/lang/String;)V") : nullptr;
    jstring charset = fromBytes ? env->NewStringUTF("UTF-8") : nullptr;
    auto* out = charset ? static_cast<jstring>(env->NewObject(type, fromBytes, bytes, charset)) : nullptr;
    env->DeleteLocalRef(charset);
    env->DeleteLocalRef(type);
    env->DeleteLocalRef(bytes);
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

}  // namespace geode::jni
