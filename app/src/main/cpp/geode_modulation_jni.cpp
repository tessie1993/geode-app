// JNI for the user's modulation (core/viz/ModDocument through api/geode_modulation_api.h): the waves, the
// envelopes and the reaction routes as their saved texts. Marshals only. The renderer's three setters are
// in geode_viz_jni.cpp, beside the handle registry they need.
#include <jni.h>

#include <vector>

#include "api/geode_modulation_api.h"
#include "jni_util.hpp"

namespace {

using geode::jni::Utf8Chars;
using geode::jni::utf8String;

// More than any picker lists: the target enum is far smaller.
constexpr int kMaxTargets = 256;
constexpr int kRouteValues = GEODE_REACT_ROUTES * GEODE_MOD_ROUTE_VALUES;
constexpr int kMusicValues = GEODE_MUSIC_MOMENTS * GEODE_MOD_MUSIC_VALUES;

// A text-out call's whole text; `call(out, capacity)` returns the length it needs. An empty text is null
// where the call means "none" by it.
template <typename Call>
jstring textOf(JNIEnv* env, bool emptyIsNull, Call call) {
    char fixed[2048];
    const int need = call(fixed, static_cast<int>(sizeof fixed));
    if (need == 0 && emptyIsNull) return nullptr;
    if (need < static_cast<int>(sizeof fixed)) return utf8String(env, fixed);
    std::vector<char> whole(static_cast<size_t>(need) + 1);
    call(whole.data(), need + 1);
    return utf8String(env, whole.data());
}

jfloatArray floatsOf(JNIEnv* env, const float* values, int count) {
    jfloatArray out = env->NewFloatArray(count);
    if (out && count > 0) env->SetFloatArrayRegion(out, 0, count, values);
    return out;
}

jintArray intsOf(JNIEnv* env, const int* values, int count) {
    jintArray out = env->NewIntArray(count);
    if (out && count > 0) env->SetIntArrayRegion(out, 0, count, reinterpret_cast<const jint*>(values));
    return out;
}

}  // namespace

extern "C" {

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_ModulationNative_modCatalogText(JNIEnv* env, jobject) {
    return utf8String(env, geode_mod_catalog_text());
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_ModulationNative_modLfosRead(JNIEnv* env, jobject, jstring text) {
    Utf8Chars saved(env, text);
    return textOf(env, false, [&](char* out, int capacity) { return geode_mod_lfos_read(saved.get(), out, capacity); });
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_ModulationNative_modAdsrsRead(JNIEnv* env, jobject, jstring text) {
    Utf8Chars saved(env, text);
    return textOf(env, false, [&](char* out, int capacity) { return geode_mod_adsrs_read(saved.get(), out, capacity); });
}

// Null for text that is no route list: Auto.
JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_ModulationNative_modReactionsRead(JNIEnv* env, jobject, jstring text) {
    Utf8Chars saved(env, text);
    return textOf(env, true, [&](char* out, int capacity) { return geode_mod_reactions_read(saved.get(), out, capacity); });
}

JNIEXPORT jfloatArray JNICALL
Java_dev_geode_engine_bridge_ModulationNative_modLfoValues(JNIEnv* env, jobject, jstring lfos, jint slot, jstring styleId) {
    Utf8Chars saved(env, lfos);
    Utf8Chars id(env, styleId);
    float out[GEODE_MOD_LFO_VALUES] = {};
    return floatsOf(env, out, geode_mod_lfo_values(saved.get(), slot, id.get(), out, GEODE_MOD_LFO_VALUES));
}

JNIEXPORT jfloatArray JNICALL
Java_dev_geode_engine_bridge_ModulationNative_modAdsrValues(JNIEnv* env, jobject, jstring adsrs, jint slot) {
    Utf8Chars saved(env, adsrs);
    float out[GEODE_MOD_ADSR_VALUES + kMaxTargets] = {};
    return floatsOf(env, out, geode_mod_adsr_values(saved.get(), slot, out, GEODE_MOD_ADSR_VALUES + kMaxTargets));
}

JNIEXPORT jintArray JNICALL
Java_dev_geode_engine_bridge_ModulationNative_modTargetsOffered(JNIEnv* env, jobject, jstring styleId, jint picker) {
    Utf8Chars id(env, styleId);
    int out[kMaxTargets] = {};
    return intsOf(env, out, geode_mod_targets_offered(id.get(), picker, out, kMaxTargets));
}

JNIEXPORT jintArray JNICALL
Java_dev_geode_engine_bridge_ModulationNative_modAdsrTargetsOffered(JNIEnv* env, jobject, jstring adsrs, jint slot, jstring styleId) {
    Utf8Chars saved(env, adsrs);
    Utf8Chars id(env, styleId);
    int out[kMaxTargets] = {};
    return intsOf(env, out, geode_mod_adsr_targets_offered(saved.get(), slot, id.get(), out, kMaxTargets));
}

JNIEXPORT jfloatArray JNICALL
Java_dev_geode_engine_bridge_ModulationNative_modRoutesPlaying(JNIEnv* env, jobject, jstring styleId, jint seed, jstring reactions,
                                                          jint mask) {
    Utf8Chars id(env, styleId);
    Utf8Chars saved(env, reactions);
    float out[kRouteValues] = {};
    const int n = geode_mod_routes_playing(id.get(), seed, saved.get(), static_cast<unsigned>(mask), out, kRouteValues);
    return floatsOf(env, out, n);
}

// outMask[0] is the mask to save beside the returned text; null (and mask 0) when every route is Auto again.
JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_ModulationNative_modReactionsEdit(JNIEnv* env, jobject, jstring styleId, jint seed, jstring reactions,
                                                          jint mask, jint op, jint a, jint b, jfloat value, jintArray outMask) {
    Utf8Chars id(env, styleId);
    Utf8Chars saved(env, reactions);
    unsigned next = 0;
    jstring text = textOf(env, true, [&](char* out, int capacity) {
        return geode_mod_reactions_edit(id.get(), seed, saved.get(), static_cast<unsigned>(mask), op, a, b, value, out,
                                        capacity, &next);
    });
    if (outMask && env->GetArrayLength(outMask) >= 1) {
        const jint hand = static_cast<jint>(next);
        env->SetIntArrayRegion(outMask, 0, 1, &hand);
    }
    return text;
}

JNIEXPORT jfloatArray JNICALL
Java_dev_geode_engine_bridge_ModulationNative_modMusicState(JNIEnv* env, jobject, jstring styleId, jint seed, jstring reactions,
                                                       jint mask) {
    Utf8Chars id(env, styleId);
    Utf8Chars saved(env, reactions);
    float out[kMusicValues] = {};
    const int n = geode_mod_music_state(id.get(), seed, saved.get(), static_cast<unsigned>(mask), out, kMusicValues);
    return floatsOf(env, out, n);
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_ModulationNative_modRecipeState(JNIEnv* env, jobject, jint recipe, jstring styleId, jint seed,
                                                        jstring reactions, jint mask) {
    Utf8Chars id(env, styleId);
    Utf8Chars saved(env, reactions);
    return static_cast<jint>(geode_mod_recipe_state(recipe, id.get(), seed, saved.get(), static_cast<unsigned>(mask)));
}

// -1: the saved routes no longer read and are to be dropped.
JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_ModulationNative_modHandLoad(JNIEnv* env, jobject, jstring reactions, jboolean hasMask, jint savedMask) {
    Utf8Chars saved(env, reactions);
    return geode_mod_hand_load(saved.get(), hasMask ? 1 : 0, savedMask);
}

// The modulation the app holds (geode_mod_state_*): each edit is an event and returns nothing.
JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_ModulationNative_modStateLoad(JNIEnv* env, jobject, jstring lfos, jstring adsrs, jstring reactions,
                                                      jint mask) {
    Utf8Chars waves(env, lfos);
    Utf8Chars envelopes(env, adsrs);
    Utf8Chars routes(env, reactions);
    geode_mod_state_load(waves.get(), envelopes.get(), routes.get(), static_cast<unsigned>(mask));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_ModulationNative_modStateLfoSet(JNIEnv*, jobject, jint slot, jint field, jfloat value) {
    geode_mod_state_lfo_set(slot, field, value);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_ModulationNative_modStateAdsrSet(JNIEnv*, jobject, jint slot, jint field, jfloat value) {
    geode_mod_state_adsr_set(slot, field, value);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_ModulationNative_modStateReactionsEdit(JNIEnv* env, jobject, jstring styleId, jint seed, jint op, jint a,
                                                               jint b, jfloat value) {
    Utf8Chars id(env, styleId);
    geode_mod_state_reactions_edit(id.get(), seed, op, a, b, value);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_ModulationNative_modStateKeepLoad(JNIEnv* env, jobject, jstring document) {
    Utf8Chars text(env, document);
    geode_mod_state_keep_load(text.get());
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_ModulationNative_modStateKeepToggle(JNIEnv* env, jobject, jstring key) {
    Utf8Chars name(env, key);
    geode_mod_state_keep_toggle(name.get());
}

// A text held; null for the routes while Auto. Asked again when an edit on another thread grew it in between.
JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_ModulationNative_modStateText(JNIEnv* env, jobject, jint which) {
    std::vector<char> text(2048);
    for (;;) {
        const int need = geode_mod_state_text(which, text.data(), static_cast<int>(text.size()));
        if (need == 0 && which == GEODE_MOD_STATE_REACTIONS) return nullptr;
        if (need < static_cast<int>(text.size())) return utf8String(env, text.data());
        text.resize(static_cast<size_t>(need) + 1);
    }
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_ModulationNative_modStateHandMask(JNIEnv*, jobject) {
    return static_cast<jint>(geode_mod_state_hand_mask());
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_ModulationNative_modLfoShows(JNIEnv* env, jobject, jstring lfos, jint slot) {
    Utf8Chars saved(env, lfos);
    return static_cast<jint>(geode_mod_lfo_shows(saved.get(), slot));
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_ModulationNative_modAdsrShows(JNIEnv* env, jobject, jstring adsrs, jint slot) {
    Utf8Chars saved(env, adsrs);
    return static_cast<jint>(geode_mod_adsr_shows(saved.get(), slot));
}

}  // extern "C"
