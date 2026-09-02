#pragma once
#include <jni.h>

namespace geode::jni {

class Utf8Chars {
public:
    Utf8Chars(JNIEnv* env, jstring str) : env_(env), str_(str), chars_(str ? env->GetStringUTFChars(str, nullptr) : nullptr) {}
    ~Utf8Chars() { if (chars_) env_->ReleaseStringUTFChars(str_, chars_); }
    Utf8Chars(const Utf8Chars&) = delete;
    Utf8Chars& operator=(const Utf8Chars&) = delete;
    const char* get() const { return chars_; }

private:
    JNIEnv* env_;
    jstring str_;
    const char* chars_;
};

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
