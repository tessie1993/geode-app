// JNI for an export's platform audio work (core/api/geode_export_audio_api.h): marshalling only.
#include <jni.h>

#include "api/geode_export_audio_api.h"

namespace {

// Layout of the double[] exportLoudnessMeasure fills; GeodeNative.exportLoudnessMeasure documents the same order.
constexpr jsize kReadingDoubles = 9;

}  // namespace

extern "C" {

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_GeodeNative_exportLoudnessMeasure(JNIEnv* env, jobject, jint fd,
                                                                                      jlong offset, jlong length,
                                                                                      jint target, jdoubleArray out) {
    geode_loudness_reading r{};
    geode_export_loudness_measure(fd, offset, length, target, &r);
    if (out && env->GetArrayLength(out) >= kReadingDoubles) {
        const jdouble values[kReadingDoubles] = {r.lufs,           r.true_peak_dbtp,
                                                 r.gain_db,        r.gain_for_level_db,
                                                 r.resulting_lufs, r.resulting_true_peak_dbtp,
                                                 r.ceiling_dbtp,   r.quiet_lu,
                                                 r.peak_warning ? 1.0 : 0.0};
        env->SetDoubleArrayRegion(out, 0, kReadingDoubles, values);
    }
    return r.sentence;
}

}
