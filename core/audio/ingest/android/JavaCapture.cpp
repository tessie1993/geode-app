#include "audio/ingest/android/JavaCapture.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <utility>
#include <vector>

#include "audio/ingest/CapturePlan.hpp"
#include "util/Log.hpp"

namespace geode::audio::ingest {

namespace {

constexpr const char* kTag = "GeodeJavaCapture";

// The Android SDK's values, by name.
constexpr jint kReadBlocking = 0;             // AudioRecord.READ_BLOCKING
constexpr jint kRecordStateRecording = 3;     // AudioRecord.RECORDSTATE_RECORDING
constexpr jint kStateInitialized = 1;         // AudioRecord.STATE_INITIALIZED
constexpr jint kChannelInMono = 0x10;         // AudioFormat.CHANNEL_IN_MONO
constexpr jint kChannelInStereo = 0x0C;       // AudioFormat.CHANNEL_IN_STEREO
constexpr jint kEncodingPcm16 = 2;            // AudioFormat.ENCODING_PCM_16BIT
constexpr jint kEncodingPcm8 = 3;             // AudioFormat.ENCODING_PCM_8BIT
constexpr jint kEncodingFloat = 4;            // AudioFormat.ENCODING_PCM_FLOAT
constexpr jint kCapturedUsages[] = {1, 14, 0};  // AudioAttributes.USAGE_MEDIA, USAGE_GAME, USAGE_UNKNOWN
constexpr jint kVisualizerSuccess = 0;        // Visualizer.SUCCESS
constexpr jint kScalingAsPlayed = 1;          // Visualizer.SCALING_MODE_AS_PLAYED
constexpr jint kOutputMixSession = 0;
constexpr int kFallbackRateHz = 48000;
constexpr int kFallbackCaptureRateMhz = 20000;
constexpr int kMilli = 1000;
constexpr jint kLocalFrame = 16;

// Logs and clears a pending exception (a refused constructor or call); true when there was one.
bool cleared(JNIEnv* env) {
    if (!env->ExceptionCheck()) return false;
    env->ExceptionDescribe();
    env->ExceptionClear();
    return true;
}

// Null, with the exception cleared, when there is no such class or method.
jclass findClass(JNIEnv* env, const char* name) {
    jclass type = env->FindClass(name);
    return cleared(env) ? nullptr : type;
}

jmethodID method(JNIEnv* env, jclass type, const char* name, const char* signature) {
    jmethodID id = type ? env->GetMethodID(type, name, signature) : nullptr;
    return cleared(env) ? nullptr : id;
}

jmethodID staticMethod(JNIEnv* env, jclass type, const char* name, const char* signature) {
    jmethodID id = type ? env->GetStaticMethodID(type, name, signature) : nullptr;
    return cleared(env) ? nullptr : id;
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
        JNIEnv* env = currentEnv();
        // begin() may have failed to attach; the Java references still have to be let go of.
        if (!env && vm_->AttachCurrentThread(&env, nullptr) == JNI_OK) attached_ = true;
        if (env) release(env);
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
        if (!record_ || !env || !stop_) return;
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
    // `visualizer` enabled with capture size captureSize; owned from here on.
    VisualizerReader(JavaVM* vm, JNIEnv* env, jobject visualizer, int captureSize, int keepBytes, int periodMs)
        : JavaReader(vm, "geode-output-mix"), size_(std::min(keepBytes, captureSize)), period_(periodMs) {
        jclass type = env->GetObjectClass(visualizer);
        getWaveForm_ = method(env, type, "getWaveForm", "([B)I");
        setEnabled_ = method(env, type, "setEnabled", "(Z)I");
        release_ = method(env, type, "release", "()V");
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

    bool ready() const { return visualizer_ && array_ && getWaveForm_ && setEnabled_ && release_ && size_ > 0; }

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
        if (visualizer_) {
            if (setEnabled_) env->CallIntMethod(visualizer_, setEnabled_, JNI_FALSE);
            cleared(env);
            if (release_) env->CallVoidMethod(visualizer_, release_);
            cleared(env);
            env->DeleteGlobalRef(visualizer_);
        }
        if (array_) env->DeleteGlobalRef(array_);
        visualizer_ = nullptr;
        array_ = nullptr;
    }

    int size_;
    std::chrono::milliseconds period_;
    jobject visualizer_ = nullptr;
    jbyteArray array_ = nullptr;
    jmethodID getWaveForm_ = nullptr;
    jmethodID setEnabled_ = nullptr;
    jmethodID release_ = nullptr;
    std::mutex waitLock_;
    std::condition_variable wake_;
    bool interrupted_ = false;
    std::chrono::steady_clock::time_point next_{};
};

JavaVM* vmOf(JNIEnv* env) {
    JavaVM* vm = nullptr;
    return env && env->GetJavaVM(&vm) == JNI_OK ? vm : nullptr;
}

// The classes and methods a playback-capture AudioRecord is built with. Their class refs are local to the
// caller's frame.
struct RecordCalls {
    jclass record = nullptr;
    jclass recordBuilder = nullptr;
    jclass formatBuilder = nullptr;
    jclass configBuilder = nullptr;
    jmethodID minBufferSize = nullptr;
    jmethodID getState = nullptr;
    jmethodID release = nullptr;
    jmethodID configNew = nullptr;
    jmethodID addMatchingUsage = nullptr;
    jmethodID configBuild = nullptr;
    jmethodID formatNew = nullptr;
    jmethodID setEncoding = nullptr;
    jmethodID setSampleRate = nullptr;
    jmethodID setChannelMask = nullptr;
    jmethodID formatBuild = nullptr;
    jmethodID recordNew = nullptr;
    jmethodID setConfig = nullptr;
    jmethodID setFormat = nullptr;
    jmethodID setBufferSize = nullptr;
    jmethodID recordBuild = nullptr;

    bool find(JNIEnv* env) {
        record = findClass(env, "android/media/AudioRecord");
        recordBuilder = findClass(env, "android/media/AudioRecord$Builder");
        formatBuilder = findClass(env, "android/media/AudioFormat$Builder");
        configBuilder = findClass(env, "android/media/AudioPlaybackCaptureConfiguration$Builder");
        minBufferSize = staticMethod(env, record, "getMinBufferSize", "(III)I");
        getState = method(env, record, "getState", "()I");
        release = method(env, record, "release", "()V");
        configNew = method(env, configBuilder, "<init>", "(Landroid/media/projection/MediaProjection;)V");
        addMatchingUsage = method(env, configBuilder, "addMatchingUsage",
                                  "(I)Landroid/media/AudioPlaybackCaptureConfiguration$Builder;");
        configBuild = method(env, configBuilder, "build", "()Landroid/media/AudioPlaybackCaptureConfiguration;");
        formatNew = method(env, formatBuilder, "<init>", "()V");
        setEncoding = method(env, formatBuilder, "setEncoding", "(I)Landroid/media/AudioFormat$Builder;");
        setSampleRate = method(env, formatBuilder, "setSampleRate", "(I)Landroid/media/AudioFormat$Builder;");
        setChannelMask = method(env, formatBuilder, "setChannelMask", "(I)Landroid/media/AudioFormat$Builder;");
        formatBuild = method(env, formatBuilder, "build", "()Landroid/media/AudioFormat;");
        recordNew = method(env, recordBuilder, "<init>", "()V");
        setConfig = method(env, recordBuilder, "setAudioPlaybackCaptureConfig",
                           "(Landroid/media/AudioPlaybackCaptureConfiguration;)Landroid/media/AudioRecord$Builder;");
        setFormat = method(env, recordBuilder, "setAudioFormat",
                           "(Landroid/media/AudioFormat;)Landroid/media/AudioRecord$Builder;");
        setBufferSize = method(env, recordBuilder, "setBufferSizeInBytes", "(I)Landroid/media/AudioRecord$Builder;");
        recordBuild = method(env, recordBuilder, "build", "()Landroid/media/AudioRecord;");
        return minBufferSize && getState && release && configNew && addMatchingUsage && configBuild && formatNew &&
               setEncoding && setSampleRate && setChannelMask && formatBuild && recordNew && setConfig && setFormat &&
               setBufferSize && recordBuild;
    }
};

// The capture configuration over `projection`, a local ref; null (cleared) when it is refused.
jobject captureConfig(JNIEnv* env, const RecordCalls& calls, jobject projection) {
    jobject builder = env->NewObject(calls.configBuilder, calls.configNew, projection);
    if (cleared(env) || !builder) return nullptr;
    for (jint usage : kCapturedUsages) {
        env->DeleteLocalRef(env->CallObjectMethod(builder, calls.addMatchingUsage, usage));
        if (cleared(env)) {
            env->DeleteLocalRef(builder);
            return nullptr;
        }
    }
    jobject config = env->CallObjectMethod(builder, calls.configBuild);
    env->DeleteLocalRef(builder);
    return cleared(env) ? nullptr : config;
}

// One format tried, in a local frame of its own: the initialized record as a local ref of the caller's frame,
// or null with anything it made released.
jobject tryRecord(JNIEnv* env, const RecordCalls& calls, jobject config, const CaptureFormat& format) {
    const jint mask = format.channels == 2 ? kChannelInStereo : kChannelInMono;
    const jint encoding = format.asFloat ? kEncodingFloat : kEncodingPcm16;
    const jint minBytes = env->CallStaticIntMethod(calls.record, calls.minBufferSize, format.rateHz, mask, encoding);
    if (cleared(env)) return nullptr;
    const int bytes = captureBufferBytes(minBytes, format.channels);
    if (bytes <= 0) return nullptr;
    if (env->PushLocalFrame(kLocalFrame) != JNI_OK) {
        cleared(env);
        return nullptr;
    }
    jobject made = nullptr;
    jobject fb = env->NewObject(calls.formatBuilder, calls.formatNew);
    if (!cleared(env) && fb) {
        env->CallObjectMethod(fb, calls.setEncoding, encoding);
        if (!cleared(env)) env->CallObjectMethod(fb, calls.setSampleRate, static_cast<jint>(format.rateHz));
        if (!cleared(env)) env->CallObjectMethod(fb, calls.setChannelMask, mask);
        jobject audioFormat = cleared(env) ? nullptr : env->CallObjectMethod(fb, calls.formatBuild);
        jobject rb = cleared(env) || !audioFormat ? nullptr : env->NewObject(calls.recordBuilder, calls.recordNew);
        if (!cleared(env) && rb) {
            env->CallObjectMethod(rb, calls.setConfig, config);
            if (!cleared(env)) env->CallObjectMethod(rb, calls.setFormat, audioFormat);
            if (!cleared(env)) env->CallObjectMethod(rb, calls.setBufferSize, static_cast<jint>(bytes));
            // build throws UnsupportedOperationException for a format the platform refuses.
            jobject record = cleared(env) ? nullptr : env->CallObjectMethod(rb, calls.recordBuild);
            if (!cleared(env) && record) {
                const jint state = env->CallIntMethod(record, calls.getState);
                if (!cleared(env) && state == kStateInitialized) {
                    made = record;
                } else {
                    env->CallVoidMethod(record, calls.release);
                    cleared(env);
                }
            }
        }
    }
    return env->PopLocalFrame(made);
}

}  // namespace

std::unique_ptr<CaptureReader> playbackCaptureReader(JNIEnv* env, jobject projection, JavaCaptureFormat* format) {
    JavaVM* vm = vmOf(env);
    if (!vm || !projection || !format) return nullptr;
    if (env->PushLocalFrame(kLocalFrame) != JNI_OK) {
        cleared(env);
        return nullptr;
    }
    std::unique_ptr<CaptureReader> made;
    RecordCalls calls;
    jobject config = calls.find(env) ? captureConfig(env, calls, projection) : nullptr;
    if (!config) GEODE_LOGW(kTag, "playback capture refused");
    CaptureFormat plan[kMaxCaptureFormats];
    const int count = config ? capturePlan(0, true, plan, kMaxCaptureFormats) : 0;
    for (int i = 0; i < count && !made; ++i) {
        jobject record = tryRecord(env, calls, config, plan[i]);
        if (!record) continue;
        const int sampleBytes = plan[i].asFloat ? 4 : 2;
        const int readBytes = kCaptureReadFrames * plan[i].channels * sampleBytes;
        // The reader owns the record from here: it is released in end when it will not record.
        auto reader = std::make_unique<AudioRecordReader>(vm, env, record, readBytes);
        env->DeleteLocalRef(record);
        if (!reader->start(env)) {
            GEODE_LOGW(kTag, "startRecording refused");
            reader->end();
            break;
        }
        *format = JavaCaptureFormat{plan[i].rateHz, plan[i].channels,
                                    plan[i].asFloat ? kEncodingFloat : kEncodingPcm16, readBytes};
        made = std::move(reader);
    }
    env->PopLocalFrame(nullptr);
    return made;
}

std::unique_ptr<CaptureReader> outputMixReader(JNIEnv* env, int maxBytes, JavaCaptureFormat* format) {
    JavaVM* vm = vmOf(env);
    if (!vm || maxBytes <= 0 || !format) return nullptr;
    if (env->PushLocalFrame(kLocalFrame) != JNI_OK) {
        cleared(env);
        return nullptr;
    }
    std::unique_ptr<CaptureReader> made;
    jclass type = findClass(env, "android/media/audiofx/Visualizer");
    const jmethodID create = method(env, type, "<init>", "(I)V");
    const jmethodID setEnabled = method(env, type, "setEnabled", "(Z)I");
    const jmethodID sizeRange = staticMethod(env, type, "getCaptureSizeRange", "()[I");
    const jmethodID setCaptureSize = method(env, type, "setCaptureSize", "(I)I");
    const jmethodID setScalingMode = method(env, type, "setScalingMode", "(I)I");
    const jmethodID samplingRate = method(env, type, "getSamplingRate", "()I");
    const jmethodID captureSize = method(env, type, "getCaptureSize", "()I");
    const jmethodID maxCaptureRate = staticMethod(env, type, "getMaxCaptureRate", "()I");
    const jmethodID release = method(env, type, "release", "()V");
    const bool found = create && setEnabled && sizeRange && setCaptureSize && setScalingMode && samplingRate &&
                       captureSize && maxCaptureRate && release;
    // The constructor throws (UnsupportedOperationException, RuntimeException) on a device that refuses the
    // output mix or while another app holds it.
    jobject visualizer = found ? env->NewObject(type, create, kOutputMixSession) : nullptr;
    if (cleared(env) || !visualizer) {
        GEODE_LOGW(kTag, "output mix refused");
        env->PopLocalFrame(nullptr);
        return nullptr;
    }
    // The capture size can only change while the effect is disabled. As played, not normalized: normalized
    // lifts every capture to full scale, so a quiet passage hit as hard as a drop and room noise never read
    // as silence.
    bool ok = true;
    env->CallIntMethod(visualizer, setEnabled, JNI_FALSE);
    ok = ok && !cleared(env);
    auto range = ok ? static_cast<jintArray>(env->CallStaticObjectMethod(type, sizeRange)) : nullptr;
    ok = ok && !cleared(env) && range && env->GetArrayLength(range) >= 2;
    jint bounds[2] = {0, 0};
    if (ok) env->GetIntArrayRegion(range, 0, 2, bounds);
    ok = ok && !cleared(env);
    if (ok) env->CallIntMethod(visualizer, setCaptureSize, std::min(bounds[1], static_cast<jint>(maxBytes)));
    ok = ok && !cleared(env);
    if (ok) env->CallIntMethod(visualizer, setScalingMode, kScalingAsPlayed);
    ok = ok && !cleared(env);
    const jint size = ok ? env->CallIntMethod(visualizer, captureSize) : 0;
    ok = ok && !cleared(env) && size > 0;
    const jint rateMhz = ok ? env->CallIntMethod(visualizer, samplingRate) : 0;
    ok = ok && !cleared(env);
    const jint captureRateMhz = ok ? env->CallStaticIntMethod(type, maxCaptureRate) : 0;
    ok = ok && !cleared(env);
    if (ok) env->CallIntMethod(visualizer, setEnabled, JNI_TRUE);
    ok = ok && !cleared(env);
    if (ok) {
        const int keep = std::min(static_cast<int>(size), maxBytes);
        const int periodMs = kMilli * kMilli / (captureRateMhz > 0 ? captureRateMhz : kFallbackCaptureRateMhz);
        auto reader = std::make_unique<VisualizerReader>(vm, env, visualizer, size, keep, std::max(periodMs, 1));
        if (reader->ready()) {
            const int rateHz = rateMhz / kMilli > 0 ? rateMhz / kMilli : kFallbackRateHz;
            *format = JavaCaptureFormat{rateHz, 1, kEncodingPcm8, keep};
            made = std::move(reader);
        } else {
            reader->end();  // disables and releases the effect
        }
    } else {
        GEODE_LOGW(kTag, "output mix setup failed");
        env->CallVoidMethod(visualizer, release);
        cleared(env);
    }
    env->PopLocalFrame(nullptr);
    return made;
}

}  // namespace geode::audio::ingest
