#include <jni.h>
#include "api/geode_api.h"

extern "C" {

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_version(JNIEnv* env, jobject) {
    return env->NewStringUTF(geode_version());
}

}
