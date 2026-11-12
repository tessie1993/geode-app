// JNI for the engine classes ported out of the Kotlin engine modules (docs/agents/engine-kotlin-audit.md).
// Marshals only.
#include <jni.h>

#include <string>

#include "api/geode_reaction_text_api.h"
#include "jni_util.hpp"

using geode::jni::FloatElements;
using geode::jni::Utf8Chars;

extern "C" {

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_reactionsTextEncode(JNIEnv* env, jobject, jfloatArray routes) {
    FloatElements data(env, routes);
    std::string text(geode_reactions_text_encode(data.get(), data.length(), nullptr, 0) + 1, '\0');
    geode_reactions_text_encode(data.get(), data.length(), text.data(), text.size());
    return env->NewStringUTF(text.c_str());
}

// Null for text that is no list (geode_reactions_text_decode's -1).
JNIEXPORT jfloatArray JNICALL
Java_dev_geode_engine_bridge_GeodeNative_reactionsTextDecode(JNIEnv* env, jobject, jstring text) {
    Utf8Chars chars(env, text);
    float out[GEODE_REACT_ROUTES * GEODE_REACT_ROUTE_FLOATS] = {};
    const int n = geode_reactions_text_decode(chars.get(), out, GEODE_REACT_ROUTES * GEODE_REACT_ROUTE_FLOATS);
    if (n < 0) return nullptr;
    jfloatArray result = env->NewFloatArray(n * GEODE_REACT_ROUTE_FLOATS);
    if (result && n > 0) env->SetFloatArrayRegion(result, 0, n * GEODE_REACT_ROUTE_FLOATS, out);
    return result;
}

}  // extern "C"
