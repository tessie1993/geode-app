#include "audio/ingest/android/JavaCapture.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <vector>

namespace geode::audio::ingest {

namespace {

constexpr jint kReadBlocking = 0;        // AudioRecord.READ_BLOCKING
constexpr jint kRecordStateRecording = 3;  // AudioRecord.RECORDSTATE_RECORDING
constexpr jint kVisualizerSuccess = 0;   // Visualizer.SUCCESS

bool cleared(JNIEnv* env) {
    if (!env->ExceptionCheck()) return false;
    env->ExceptionClear();
    return true;
}

// Null, with the NoSuchMethodError cleared, when the class has no such method.
jmethodID method(JNIEnv* env, jclass type, const char* name, const char* signature) {
    jmethodID id = type ? env->GetMethodID(type, name, signature) : nullptr;
    cleared(env);
    return id;
}

// The pump's thread attached to the VM between begin and end; the Java object's references are let go of in
// end, on whichever attached thread runs it.
class JavaReader : public CaptureReader {
public:
    JavaReader(JavaVM* vm, const char* name) : vm_(vm), name_(name) {}

    bool begin() override {
        if (vm_->GetEnv(reinterpret_cast<void**>(&env_), JNI_VERSION_1_6) == JNI_OK) return true;
        JavaVMAttachArgs args{JNI_VERSION_1_6, name_, nullptr};
        if (vm_->AttachCurrentThread(&env_, &args) != JNI_OK) {
            env_ = nullptr;
            return false;
        }
        attached_ = true;
        return true;
    }

    void end() override {
        if (JNIEnv* env = currentEnv()) release(env);
        if (attached_) vm_->DetachCurrentThread();
        attached_ = false;
        env_ = nullptr;
    }

protected:
    virtual void release(JNIEnv* env) = 0;

    JNIEnv* currentEnv() const {
        JNIEnv* env = nullptr;
        return vm_->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK ? env : nullptr;
    }

    JavaVM* vm_;
    const char* name_;
    JNIEnv* env_ = nullptr;  // the pump thread's, between begin and end

private:
    bool attached_ = false;
};

class AudioRecordReader final : public JavaReader {
public:
    AudioRecordReader(JavaVM* vm, JNIEnv* env, jobject record, int readBytes)
        : JavaReader(vm, "geode-playback-capture"), bytes_(static_cast<size_t>(readBytes)) {
        jclass type = env->GetObjectClass(record);
        read_ = method(env, type, "read", "(Ljava/nio/ByteBuffer;II)I");
        stop_ = method(env, type, "stop", "()V");
        release_ = method(env, type, "release", "()V");
        start_ = method(env, type, "startRecording", "()V");
        state_ = method(env, type, "getRecordingState", "()I");
        env->DeleteLocalRef(type);
        record_ = env->NewGlobalRef(record);
        jobject direct = env->NewDirectByteBuffer(bytes_.data(), static_cast<jlong>(bytes_.size()));
        cleared(env);
        if (direct) {
            buffer_ = env->NewGlobalRef(direct);
            env->DeleteLocalRef(direct);
        }
    }

    // On the caller's thread, before the pump: startRecording, and whether the record is recording now.
    bool start(JNIEnv* env) {
        if (!record_ || !buffer_ || !read_ || !stop_ || !release_ || !start_ || !state_) return false;
        env->CallVoidMethod(record_, start_);
        if (cleared(env)) return false;
        const jint state = env->CallIntMethod(record_, state_);
        return !cleared(env) && state == kRecordStateRecording;
    }

    int read(void* data, int capacity) override {
        if (!env_ || !record_) return -1;
        const jint ask = std::min(capacity, static_cast<int>(bytes_.size()));
        const jint n = env_->CallIntMethod(record_, read_, buffer_, ask, kReadBlocking);
        if (cleared(env_) || n < 0) return -1;
        const int got = std::min(n, ask);
        if (got > 0) std::memcpy(data, bytes_.data(), static_cast<size_t>(got));
        return got;
    }

    // AudioRecord.stop returns a blocking read.
    void interrupt() override {
        std::lock_guard<std::mutex> lock(lock_);
        JNIEnv* env = currentEnv();
        if (!record_ || !env) return;
        env->CallVoidMethod(record_, stop_);
        cleared(env);
    }

private:
    void release(JNIEnv* env) override {
        std::lock_guard<std::mutex> lock(lock_);
        if (record_) {
            if (stop_) env->CallVoidMethod(record_, stop_);
            cleared(env);
            if (release_) env->CallVoidMethod(record_, release_);
            cleared(env);
            env->DeleteGlobalRef(record_);
            record_ = nullptr;
        }
        if (buffer_) env->DeleteGlobalRef(buffer_);
        buffer_ = nullptr;
    }

    std::vector<unsigned char> bytes_;
    std::mutex lock_;  // record_ between interrupt and release
    jobject record_ = nullptr;
    jobject buffer_ = nullptr;
    jmethodID read_ = nullptr;
    jmethodID stop_ = nullptr;
    jmethodID release_ = nullptr;
    jmethodID start_ = nullptr;
    jmethodID state_ = nullptr;
};

class VisualizerReader final : public JavaReader {
public:
    VisualizerReader(JavaVM* vm, JNIEnv* env, jobject visualizer, int captureSize, int keepBytes, int periodMs)
        : JavaReader(vm, "geode-output-mix"), size_(std::min(keepBytes, captureSize)), period_(periodMs) {
        jclass type = env->GetObjectClass(visualizer);
        getWaveForm_ = method(env, type, "getWaveForm", "([B)I");
        env->DeleteLocalRef(type);
        visualizer_ = env->NewGlobalRef(visualizer);
        // getWaveForm fills a whole capture whatever the array's length, so the array holds one.
        jbyteArray array = env->NewByteArray(captureSize);
        cleared(env);
        if (array) {
            array_ = static_cast<jbyteArray>(env->NewGlobalRef(array));
            env->DeleteLocalRef(array);
        }
    }

    bool ready() const { return visualizer_ && array_ && getWaveForm_; }

    int read(void* data, int capacity) override {
        {
            std::unique_lock<std::mutex> lock(waitLock_);
            if (wake_.wait_until(lock, next_, [this] { return interrupted_; })) return 0;
            next_ = std::chrono::steady_clock::now() + period_;
        }
        if (!env_ || !visualizer_) return -1;
        const jint status = env_->CallIntMethod(visualizer_, getWaveForm_, array_);
        if (cleared(env_)) return -1;
        // A capture the effect could not give this time is skipped, as the listener skipped it.
        if (status != kVisualizerSuccess) return 0;
        const int n = std::min(size_, capacity);
        env_->GetByteArrayRegion(array_, 0, n, static_cast<jbyte*>(data));
        return cleared(env_) ? -1 : n;
    }

    void interrupt() override {
        {
            std::lock_guard<std::mutex> lock(waitLock_);
            interrupted_ = true;
        }
        wake_.notify_all();
    }

private:
    void release(JNIEnv* env) override {
        if (visualizer_) env->DeleteGlobalRef(visualizer_);
        if (array_) env->DeleteGlobalRef(array_);
        visualizer_ = nullptr;
        array_ = nullptr;
    }

    int size_;
    std::chrono::milliseconds period_;
    jobject visualizer_ = nullptr;
    jbyteArray array_ = nullptr;
    jmethodID getWaveForm_ = nullptr;
    std::mutex waitLock_;
    std::condition_variable wake_;
    bool interrupted_ = false;
    std::chrono::steady_clock::time_point next_{};
};

JavaVM* vmOf(JNIEnv* env) {
    JavaVM* vm = nullptr;
    return env && env->GetJavaVM(&vm) == JNI_OK ? vm : nullptr;
}

}  // namespace

std::unique_ptr<CaptureReader> audioRecordReader(JNIEnv* env, jobject record, int readBytes) {
    JavaVM* vm = vmOf(env);
    if (!vm || !record) return nullptr;
    auto reader = std::make_unique<AudioRecordReader>(vm, env, record, std::max(readBytes, 1));
    if (readBytes > 0 && reader->start(env)) return reader;
    reader->end();
    return nullptr;
}

std::unique_ptr<CaptureReader> visualizerReader(JNIEnv* env, jobject visualizer, int captureSize, int keepBytes,
                                                int periodMs) {
    JavaVM* vm = vmOf(env);
    if (!vm || !visualizer || captureSize <= 0 || keepBytes <= 0) return nullptr;
    auto reader =
        std::make_unique<VisualizerReader>(vm, env, visualizer, captureSize, keepBytes, std::max(periodMs, 1));
    if (reader->ready()) return reader;
    reader->end();
    return nullptr;
}

}  // namespace geode::audio::ingest
