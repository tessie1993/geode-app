#include <jni.h>
#include <android/asset_manager_jni.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "api/geode_api.h"
#include "jni_util.hpp"

namespace {

// A palette swatch's colours as a Java int[]; at most kMaxSwatch, which no swatch needs more of.
jintArray toJava(JNIEnv* env, const std::vector<int>& values) {
    jintArray out = env->NewIntArray(static_cast<jsize>(values.size()));
    if (out) env->SetIntArrayRegion(out, 0, static_cast<jsize>(values.size()), reinterpret_cast<const jint*>(values.data()));
    return out;
}
constexpr jint kMaxSwatch = 256;


using geode::jni::FloatElements;
using geode::jni::Utf8Chars;

// Liveness of every renderer handed to Kotlin (#853). Kotlin checks `handle != 0L` and then calls,
// and destroy() on another thread can run between the two; a raw pointer would then be freed
// memory. Each handle has a reader/writer lock: every call holds it shared for the whole native
// call, destroy takes it exclusive after unregistering, so it waits for calls in flight and a
// later (stale) handle finds nothing and becomes a no-op.
struct VizEntry {
    std::shared_mutex lock;
};

std::mutex gVizMutex;
std::unordered_map<geode_viz*, std::shared_ptr<VizEntry>> gVizLive;

// A live renderer held for the length of the full expression it appears in; null for a stale handle.
class VizRef {
public:
    explicit VizRef(jlong handle) {
        auto* v = reinterpret_cast<geode_viz*>(handle);
        {
            std::lock_guard<std::mutex> guard(gVizMutex);
            auto it = gVizLive.find(v);
            if (it == gVizLive.end()) return;
            entry_ = it->second;
        }
        lock_ = std::shared_lock<std::shared_mutex>(entry_->lock);
        viz_ = v;
    }
    VizRef(const VizRef&) = delete;
    VizRef& operator=(const VizRef&) = delete;
    operator geode_viz*() const { return viz_; }

private:
    std::shared_ptr<VizEntry> entry_;  // declared before lock_: the lock is released first
    std::shared_lock<std::shared_mutex> lock_;
    geode_viz* viz_ = nullptr;
};

VizRef vizOf(jlong handle) { return VizRef(handle); }

void vizRegister(geode_viz* v) {
    if (!v) return;
    std::lock_guard<std::mutex> guard(gVizMutex);
    gVizLive[v] = std::make_shared<VizEntry>();
}

void vizDestroyLive(jlong handle) {
    auto* v = reinterpret_cast<geode_viz*>(handle);
    std::shared_ptr<VizEntry> entry;
    {
        std::lock_guard<std::mutex> guard(gVizMutex);
        auto it = gVizLive.find(v);
        if (it == gVizLive.end()) return;  // stale or already destroyed
        entry = std::move(it->second);
        gVizLive.erase(it);
    }
    std::unique_lock<std::shared_mutex> exclusive(entry->lock);  // waits for calls in flight
    geode_viz_destroy(v);
}
geode_export_pass* passOf(jlong handle) { return reinterpret_cast<geode_export_pass*>(handle); }
AAssetManager* assetsOf(JNIEnv* env, jobject assetManager) { return assetManager ? AAssetManager_fromJava(env, assetManager) : nullptr; }

// A direct ByteBuffer's bytes when it holds at least `needed` of them, else null (a heap buffer has no address).
uint8_t* directBytes(JNIEnv* env, jobject buffer, int64_t needed) {
    if (!buffer || needed <= 0) return nullptr;
    auto* bytes = static_cast<uint8_t*>(env->GetDirectBufferAddress(buffer));
    return bytes && env->GetDirectBufferCapacity(buffer) >= needed ? bytes : nullptr;
}

int64_t rgbaBytes(jint width, jint height) { return static_cast<int64_t>(width) * static_cast<int64_t>(height) * 4; }

// A Java String[] as owned UTF-8 strings (a null element becomes "").
std::vector<std::string> utf8Strings(JNIEnv* env, jobjectArray array) {
    std::vector<std::string> out;
    const jsize n = array ? env->GetArrayLength(array) : 0;
    out.reserve(static_cast<size_t>(n));
    for (jsize i = 0; i < n; ++i) {
        auto str = static_cast<jstring>(env->GetObjectArrayElement(array, i));
        {
            Utf8Chars chars(env, str);
            out.emplace_back(chars.get() ? chars.get() : "");
        }
        if (str) env->DeleteLocalRef(str);
    }
    return out;
}

std::vector<const char*> cStrings(const std::vector<std::string>& strings) {
    std::vector<const char*> out;
    out.reserve(strings.size());
    for (const auto& s : strings) out.push_back(s.c_str());
    return out;
}

}  // namespace

extern "C" {

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizCreate(JNIEnv* env, jobject, jobject assetManager, jstring cacheDir) {
    Utf8Chars dir(env, cacheDir);
    AAssetManager* assets = assetManager ? AAssetManager_fromJava(env, assetManager) : nullptr;
    geode_viz* created = geode_viz_create(assets, dir.get());
    vizRegister(created);
    return reinterpret_cast<jlong>(created);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizDestroy(JNIEnv*, jobject, jlong handle) {
    vizDestroyLive(handle);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSetParams(JNIEnv* env, jobject, jlong handle, jfloatArray values) {
    FloatElements data(env, values);
    if (data.get()) geode_viz_set_params(vizOf(handle), data.get(), data.length());
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSetReducedMotion(JNIEnv*, jobject, jlong handle, jboolean on) {
    geode_viz_set_reduced_motion(vizOf(handle), on == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_layerSet(JNIEnv* env, jobject, jstring sceneId, jfloat mix, jint blendMode) {
    Utf8Chars id(env, sceneId);
    geode_layer_set(id.get() ? id.get() : "", mix, blendMode);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSetTransition(JNIEnv* env, jobject, jlong handle, jstring id, jlong durationMs) {
    Utf8Chars name(env, id);
    geode_viz_set_transition(vizOf(handle), name.get(), durationMs);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizBeginParamMorph(JNIEnv*, jobject, jlong handle, jfloat seconds) {
    geode_viz_begin_param_morph(vizOf(handle), seconds);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSetTouch(JNIEnv* env, jobject, jlong handle, jfloatArray xy, jint points) {
    FloatElements data(env, xy);
    const jint available = data.get() ? data.length() / 2 : 0;
    geode_viz_set_touch(vizOf(handle), data.get(), points < available ? points : available);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSetTouchSmear(JNIEnv*, jobject, jlong handle, jboolean on, jfloat strength) {
    geode_viz_set_touch_smear(vizOf(handle), on == JNI_TRUE ? 1 : 0, strength);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSetFluidInjection(JNIEnv* env, jobject, jlong handle, jstring force, jstring dye) {
    Utf8Chars forceSrc(env, force);
    Utf8Chars dyeSrc(env, dye);
    geode_viz_set_fluid_injection(vizOf(handle), forceSrc.get(), dyeSrc.get());
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizLoadMilkPreset(JNIEnv* env, jobject, jlong handle, jstring path) {
    Utf8Chars file(env, path);
    geode_viz_load_milk_preset(vizOf(handle), file.get());
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizReloadMilkPreset(JNIEnv*, jobject, jlong handle) {
    geode_viz_reload_milk_preset(vizOf(handle));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSetMilkTextureDir(JNIEnv* env, jobject, jlong handle, jstring dir) {
    Utf8Chars path(env, dir);
    geode_viz_set_milk_texture_dir(vizOf(handle), path.get());
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizTakeMilkPresetLoaded(JNIEnv* env, jobject, jlong handle) {
    // One call: the take hands the path out once, so a length query first would leave the copy
    // empty. A path longer than PATH_MAX cannot be a file the renderer loaded.
    char buffer[4096];
    const size_t length = geode_viz_take_milk_preset_loaded(vizOf(handle), buffer, sizeof(buffer));
    if (length == 0 || length >= sizeof(buffer)) return nullptr;
    return geode::jni::utf8String(env, buffer);
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizTakeDirectorStyleMoments(JNIEnv*, jobject, jlong handle) {
    return geode_viz_take_director_style_moments(vizOf(handle));
}

// Region copy: every rendered frame, a few hundred frames of audio; never pins the whole scratch array.
JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizWriteAudio(JNIEnv* env, jobject, jlong handle, jfloatArray interleaved,
                                                       jint frames, jint channels, jint sampleRate) {
    if (!interleaved || frames <= 0 || channels <= 0) return;
    const jint available = env->GetArrayLength(interleaved) / channels;
    const jint count = frames < available ? frames : available;
    std::vector<float> data(static_cast<size_t>(count) * static_cast<size_t>(channels));
    env->GetFloatArrayRegion(interleaved, 0, static_cast<jsize>(data.size()), data.data());
    geode_viz_write_audio(vizOf(handle), data.data(), count, channels, sampleRate);
}

// An export's track audio: the feed hands the renderer's engine its frames natively.
JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizHearTrack(JNIEnv*, jobject, jlong handle, jlong feed) {
    return geode_viz_hear_track(vizOf(handle), reinterpret_cast<geode_track_feed*>(feed)) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizEndPreRoll(JNIEnv*, jobject, jlong handle, jlong rangeStartMs) {
    return geode_viz_end_pre_roll(vizOf(handle), rangeStartMs) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSetSoundTuning(JNIEnv* env, jobject, jlong handle, jfloatArray tuning) {
    if (!tuning || env->GetArrayLength(tuning) < GEODE_SOUND_TUNING_FLOATS) return;
    GeodeSoundTuning t{};
    env->GetFloatArrayRegion(tuning, 0, GEODE_SOUND_TUNING_FLOATS, reinterpret_cast<jfloat*>(&t));
    geode_viz_set_sound_tuning(vizOf(handle), &t);
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_reactSourceCatalog(JNIEnv* env, jobject) {
    const int need = geode_react_source_catalog(nullptr, 0);
    std::vector<char> text(static_cast<size_t>(need) + 1);
    geode_react_source_catalog(text.data(), need + 1);
    return env->NewStringUTF(text.data());
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_macroBankSet(JNIEnv* env, jobject, jstring bank) {
    Utf8Chars text(env, bank);
    geode_macro_bank_set(text.get());
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_macroBankDefault(JNIEnv* env, jobject) {
    return env->NewStringUTF(geode_macro_bank_default());
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_macroBankNormalize(JNIEnv* env, jobject, jstring bank) {
    Utf8Chars text(env, bank);
    return env->NewStringUTF(geode_macro_bank_normalize(text.get()));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_macroBankSetValue(JNIEnv* env, jobject, jstring bank, jint knobId, jfloat value) {
    Utf8Chars text(env, bank);
    return env->NewStringUTF(geode_macro_bank_set_value(text.get(), knobId, value));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_macroBankRename(JNIEnv* env, jobject, jstring bank, jint knobId, jstring name) {
    Utf8Chars text(env, bank);
    Utf8Chars label(env, name);
    return env->NewStringUTF(geode_macro_bank_rename(text.get(), knobId, label.get()));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_macroBankAddKnob(JNIEnv* env, jobject, jstring bank, jstring name) {
    Utf8Chars text(env, bank);
    Utf8Chars label(env, name);
    return env->NewStringUTF(geode_macro_bank_add_knob(text.get(), label.get()));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_macroBankRemoveKnob(JNIEnv* env, jobject, jstring bank, jint knobId) {
    Utf8Chars text(env, bank);
    return env->NewStringUTF(geode_macro_bank_remove_knob(text.get(), knobId));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_macroBankDuplicateKnob(JNIEnv* env, jobject, jstring bank, jint knobId) {
    Utf8Chars text(env, bank);
    return env->NewStringUTF(geode_macro_bank_duplicate_knob(text.get(), knobId));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_macroBankResetKnob(JNIEnv* env, jobject, jstring bank, jint knobId) {
    Utf8Chars text(env, bank);
    return env->NewStringUTF(geode_macro_bank_reset_knob(text.get(), knobId));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_macroBankMoveKnob(JNIEnv* env, jobject, jstring bank, jint knobId, jint delta) {
    Utf8Chars text(env, bank);
    return env->NewStringUTF(geode_macro_bank_move_knob(text.get(), knobId, delta));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_macroBankSetLink(JNIEnv* env, jobject, jstring bank, jint knobId, jstring target, jfloat up,
                                                          jfloat down) {
    Utf8Chars text(env, bank);
    Utf8Chars name(env, target);
    return env->NewStringUTF(geode_macro_bank_set_link(text.get(), knobId, name.get(), up, down));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_macroBankAddLink(JNIEnv* env, jobject, jstring bank, jint knobId, jstring target) {
    Utf8Chars text(env, bank);
    Utf8Chars name(env, target);
    return env->NewStringUTF(geode_macro_bank_add_link(text.get(), knobId, name.get()));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_macroBankRemoveLink(JNIEnv* env, jobject, jstring bank, jint knobId, jstring target) {
    Utf8Chars text(env, bank);
    Utf8Chars name(env, target);
    return env->NewStringUTF(geode_macro_bank_remove_link(text.get(), knobId, name.get()));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_macroBankEditor(JNIEnv* env, jobject, jstring bank, jstring styleId) {
    Utf8Chars text(env, bank);
    Utf8Chars style(env, styleId);
    return env->NewStringUTF(geode_macro_bank_editor(text.get(), style.get()));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_macroBankSetCurve(JNIEnv* env, jobject, jstring bank, jint knobId, jstring target, jstring curve) {
    Utf8Chars text(env, bank);
    Utf8Chars name(env, target);
    Utf8Chars shape(env, curve);
    return env->NewStringUTF(geode_macro_bank_set_curve(text.get(), knobId, name.get(), shape.get()));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_macroBankAddKnobFor(JNIEnv* env, jobject, jstring bank, jstring control) {
    Utf8Chars text(env, bank);
    Utf8Chars key(env, control);
    return env->NewStringUTF(geode_macro_bank_add_knob_for(text.get(), key.get()));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_macroBankControls(JNIEnv* env, jobject, jstring bank) {
    Utf8Chars text(env, bank);
    return env->NewStringUTF(geode_macro_bank_controls(text.get()));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_macroBankLiveMods(JNIEnv* env, jobject) {
    return env->NewStringUTF(geode_macro_bank_live_mods());
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_macroBankRollRanges(JNIEnv* env, jobject, jstring bank, jint knobId, jlong seed) {
    Utf8Chars text(env, bank);
    return env->NewStringUTF(geode_macro_bank_roll_ranges(text.get(), knobId, static_cast<long long>(seed)));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_macroBankAddRandom(JNIEnv* env, jobject, jstring bank, jint knobId, jstring styleId, jint count,
                                                           jlong seed) {
    Utf8Chars text(env, bank);
    Utf8Chars style(env, styleId);
    return env->NewStringUTF(geode_macro_bank_add_random(text.get(), knobId, style.get(), count, static_cast<long long>(seed)));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_macroBankRollValues(JNIEnv* env, jobject, jstring bank, jstring styleId, jlong seed) {
    Utf8Chars text(env, bank);
    Utf8Chars style(env, styleId);
    return env->NewStringUTF(geode_macro_bank_roll_values(text.get(), style.get(), static_cast<long long>(seed)));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_macroBankHideKnob(JNIEnv* env, jobject, jstring bank, jint knobId) {
    Utf8Chars text(env, bank);
    return env->NewStringUTF(geode_macro_bank_hide_knob(text.get(), knobId));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_macroBankShowKnob(JNIEnv* env, jobject, jstring bank, jint knobId) {
    Utf8Chars text(env, bank);
    return env->NewStringUTF(geode_macro_bank_show_knob(text.get(), knobId));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_macroBankAutoName(JNIEnv* env, jobject, jstring bank, jint knobId) {
    Utf8Chars text(env, bank);
    return env->NewStringUTF(geode_macro_bank_auto_name(text.get(), knobId));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_directorSet(JNIEnv*, jobject, jboolean on, jfloat amount, jlong seed) {
    geode_director_set(on == JNI_TRUE ? 1 : 0, amount, static_cast<long long>(seed));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_directorReachSet(JNIEnv*, jobject, jint level) {
    geode_director_reach_set(level);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_directorKeepSet(JNIEnv* env, jobject, jstring keys) {
    Utf8Chars text(env, keys);
    geode_director_keep_set(text.get());
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_noteResponseSet(JNIEnv*, jobject, jboolean enabled) {
    geode_note_response_set(enabled ? 1 : 0);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_shaderOverrideSet(JNIEnv* env, jobject, jstring styleId,
                                                           jstring fragmentSource) {
    Utf8Chars id(env, styleId);
    Utf8Chars source(env, fragmentSource);
    geode_shader_override_set(id.get(), source.get());
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_shaderOverride(JNIEnv* env, jobject, jstring styleId) {
    Utf8Chars id(env, styleId);
    const size_t length = geode_shader_override(id.get(), nullptr, 0);
    if (length == 0) return nullptr;
    std::vector<char> buffer(length + 1, '\0');
    geode_shader_override(id.get(), buffer.data(), buffer.size());
    return geode::jni::utf8String(env, buffer.data());
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSetLfo(JNIEnv* env, jobject, jlong handle, jint slot, jfloatArray config) {
    FloatElements data(env, config);
    if (data.get()) geode_viz_set_lfo(vizOf(handle), slot, data.get(), data.length());
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSetUserReactions(JNIEnv* env, jobject, jlong handle, jfloatArray routes) {
    FloatElements data(env, routes);
    if (routes && !data.get()) return;  // could not read the list: keep the last one
    geode_viz_set_user_reactions(vizOf(handle), data.get(), data.length(), routes ? 1 : 0);
}

JNIEXPORT jfloatArray JNICALL
Java_dev_geode_engine_bridge_GeodeNative_styleReactions(JNIEnv* env, jobject, jstring styleId, jint seed) {
    Utf8Chars id(env, styleId);
    float routes[GEODE_REACT_ROUTES * GEODE_REACT_ROUTE_FLOATS] = {};
    const int n = id.get() ? geode_style_reactions(id.get(), seed, routes, GEODE_REACT_ROUTES * GEODE_REACT_ROUTE_FLOATS) : 0;
    jfloatArray out = env->NewFloatArray(n * GEODE_REACT_ROUTE_FLOATS);
    if (out && n > 0) env->SetFloatArrayRegion(out, 0, n * GEODE_REACT_ROUTE_FLOATS, routes);
    return out;
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_handSet(JNIEnv*, jobject, jint mask, jboolean hasUser) {
    geode_hand_set(static_cast<unsigned>(mask), hasUser ? 1 : 0);
}

JNIEXPORT jfloatArray JNICALL
Java_dev_geode_engine_bridge_GeodeNative_styleReactionsHand(JNIEnv* env, jobject, jstring styleId, jint seed, jfloatArray user, jint mask) {
    Utf8Chars id(env, styleId);
    FloatElements routes(env, user);
    float out[GEODE_REACT_ROUTES * GEODE_REACT_ROUTE_FLOATS] = {};
    const int n = id.get() ? geode_style_reactions_hand(id.get(), seed, routes.get(), routes.length(), user ? 1 : 0,
                                                        static_cast<unsigned>(mask), out, GEODE_REACT_ROUTES * GEODE_REACT_ROUTE_FLOATS)
                           : 0;
    jfloatArray result = env->NewFloatArray(n * GEODE_REACT_ROUTE_FLOATS);
    if (result && n > 0) env->SetFloatArrayRegion(result, 0, n * GEODE_REACT_ROUTE_FLOATS, out);
    return result;
}

JNIEXPORT jfloatArray JNICALL
Java_dev_geode_engine_bridge_GeodeNative_musicState(JNIEnv* env, jobject, jstring styleId, jint seed, jfloatArray user, jint mask) {
    Utf8Chars id(env, styleId);
    FloatElements routes(env, user);
    float out[GEODE_MUSIC_MOMENTS * GEODE_MUSIC_STATE_FLOATS] = {};
    const int n = id.get() ? geode_music_state(id.get(), seed, routes.get(), routes.length(), user ? 1 : 0,
                                               static_cast<unsigned>(mask), out, GEODE_MUSIC_MOMENTS * GEODE_MUSIC_STATE_FLOATS)
                           : 0;
    jfloatArray result = env->NewFloatArray(n * GEODE_MUSIC_STATE_FLOATS);
    if (result && n > 0) env->SetFloatArrayRegion(result, 0, n * GEODE_MUSIC_STATE_FLOATS, out);
    return result;
}

// outHand[0] is the new mask, outHand[1] 1 while the user has a list (0: all Auto); returns the user's routes.
JNIEXPORT jfloatArray JNICALL
Java_dev_geode_engine_bridge_GeodeNative_musicEdit(JNIEnv* env, jobject, jstring styleId, jint seed, jfloatArray user, jint mask, jint op,
                                                   jint moment, jint action, jfloat value, jintArray outHand) {
    Utf8Chars id(env, styleId);
    FloatElements routes(env, user);
    float out[GEODE_REACT_ROUTES * GEODE_REACT_ROUTE_FLOATS] = {};
    unsigned nextMask = 0;
    int hasUser = 0;
    // A null id leaves the user's side as it was (geode_music_edit's own rule), never "all Auto" by accident.
    const int n = geode_music_edit(id.get(), seed, routes.get(), routes.length(), user ? 1 : 0, static_cast<unsigned>(mask), op,
                                   moment, action, value, out, GEODE_REACT_ROUTES * GEODE_REACT_ROUTE_FLOATS, &nextMask, &hasUser);
    if (outHand && env->GetArrayLength(outHand) >= 2) {
        const jint hand[2] = {static_cast<jint>(nextMask), hasUser};
        env->SetIntArrayRegion(outHand, 0, 2, hand);
    }
    jfloatArray result = env->NewFloatArray(n * GEODE_REACT_ROUTE_FLOATS);
    if (result && n > 0) env->SetFloatArrayRegion(result, 0, n * GEODE_REACT_ROUTE_FLOATS, out);
    return result;
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_musicActionsFor(JNIEnv* env, jobject, jstring styleId) {
    Utf8Chars id(env, styleId);
    return static_cast<jint>(geode_music_actions_for(id.get()));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_modTargetsText(JNIEnv* env, jobject) {
    return env->NewStringUTF(geode_mod_targets_text());
}

// A null styleId gives every route of the recipe (geode_event_recipe_routes' own rule).
JNIEXPORT jfloatArray JNICALL
Java_dev_geode_engine_bridge_GeodeNative_eventRecipeRoutes(JNIEnv* env, jobject, jint recipe, jstring styleId) {
    Utf8Chars id(env, styleId);
    float out[GEODE_REACT_ROUTES * GEODE_REACT_ROUTE_FLOATS] = {};
    const int n = geode_event_recipe_routes(recipe, id.get(), out, GEODE_REACT_ROUTES * GEODE_REACT_ROUTE_FLOATS);
    jfloatArray result = env->NewFloatArray(n * GEODE_REACT_ROUTE_FLOATS);
    if (result && n > 0) env->SetFloatArrayRegion(result, 0, n * GEODE_REACT_ROUTE_FLOATS, out);
    return result;
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_eventRecipeState(JNIEnv* env, jobject, jint recipe, jstring styleId, jfloatArray current) {
    Utf8Chars id(env, styleId);
    FloatElements routes(env, current);
    return static_cast<jint>(geode_event_recipe_state(recipe, id.get(), routes.get(), routes.length()));
}

JNIEXPORT jfloatArray JNICALL
Java_dev_geode_engine_bridge_GeodeNative_eventRecipeToggle(JNIEnv* env, jobject, jint recipe, jstring styleId, jfloatArray current) {
    Utf8Chars id(env, styleId);
    FloatElements routes(env, current);
    float out[GEODE_REACT_ROUTES * GEODE_REACT_ROUTE_FLOATS] = {};
    const int n = geode_event_recipe_toggle(recipe, id.get(), routes.get(), routes.length(), out,
                                            GEODE_REACT_ROUTES * GEODE_REACT_ROUTE_FLOATS);
    jfloatArray result = env->NewFloatArray(n * GEODE_REACT_ROUTE_FLOATS);
    if (result && n > 0) env->SetFloatArrayRegion(result, 0, n * GEODE_REACT_ROUTE_FLOATS, out);
    return result;
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizGyroPush(JNIEnv*, jobject, jlong handle, jfloat x, jfloat y, jfloat z, jfloat w,
                                                    jint displayRotation) {
    geode_viz_gyro_push(vizOf(handle), x, y, z, w, displayRotation);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizGyroOff(JNIEnv*, jobject, jlong handle) {
    geode_viz_gyro_off(vizOf(handle));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizRequestScene(JNIEnv* env, jobject, jlong handle, jstring sceneId) {
    Utf8Chars id(env, sceneId);
    geode_viz_request_scene(vizOf(handle), id.get());
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSetQuality(JNIEnv*, jobject, jlong handle, jfloat renderScale, jfloat marchScale,
                                                      jboolean postFx, jboolean adaptive, jint liquidHotPolicy) {
    geode_viz_set_quality(vizOf(handle), renderScale, marchScale, postFx == JNI_TRUE ? 1 : 0, adaptive == JNI_TRUE ? 1 : 0,
                          liquidHotPolicy);
}

// The live frame: the renderer takes from the ring itself, so no sample crosses JNI. A ring handle is valid
// while its Kotlin holder is reachable, and the caller holds it for this call.
JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizFrame(JNIEnv*, jobject, jlong handle, jlong ring, jfloat pacedFps, jfloat capFps) {
    return geode_viz_frame(vizOf(handle), reinterpret_cast<const geode_pcm_ring*>(ring), pacedFps, capFps);
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizLiquidMode(JNIEnv*, jobject, jlong handle) {
    return geode_viz_liquid_mode(vizOf(handle));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSetAdsr(JNIEnv* env, jobject, jlong handle, jint slot, jfloatArray config) {
    FloatElements data(env, config);
    if (data.get()) geode_viz_set_adsr(vizOf(handle), slot, data.get(), data.length());
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizPinEngineSettings(JNIEnv*, jobject, jlong handle, jboolean entitled,
                                                              jlong durationMs) {
    return static_cast<jint>(
        geode_viz_pin_engine_settings(vizOf(handle), entitled == JNI_TRUE ? 1 : 0, static_cast<long long>(durationMs)));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSetOffscreen(JNIEnv*, jobject, jlong handle, jboolean on) {
    geode_viz_set_offscreen(vizOf(handle), on == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSetFrameRate(JNIEnv*, jobject, jlong handle, jint fps) {
    geode_viz_set_frame_rate(vizOf(handle), fps);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSetUiBlurRegion(JNIEnv*, jobject, jlong handle, jfloat left, jfloat top,
                                                            jfloat right, jfloat bottom, jfloat strength, jfloat cornerPx) {
    geode_viz_set_ui_blur_region(vizOf(handle), left, top, right, bottom, strength, cornerPx);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSetEdgeGlow(JNIEnv* env, jobject, jlong handle, jfloatArray opts) {
    if (!opts) {
        geode_viz_set_edge_glow(vizOf(handle), nullptr, 0);
        return;
    }
    const jsize count = std::min<jsize>(env->GetArrayLength(opts), GEODE_EDGE_GLOW_FLOATS);
    float values[GEODE_EDGE_GLOW_FLOATS] = {};
    env->GetFloatArrayRegion(opts, 0, count, reinterpret_cast<jfloat*>(values));
    geode_viz_set_edge_glow(vizOf(handle), values, static_cast<int>(count));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_wallpaperWindow(JNIEnv* env, jobject, jfloat surfaceW, jfloat surfaceH,
                                                         jfloat displayW, jfloat displayH, jfloatArray offsets,
                                                         jfloatArray out) {
    if (!out || env->GetArrayLength(out) < 4) return;
    float raw[GEODE_WALLPAPER_OFFSETS] = {};
    jsize count = 0;
    if (offsets) {
        count = std::min<jsize>(env->GetArrayLength(offsets), GEODE_WALLPAPER_OFFSETS);
        env->GetFloatArrayRegion(offsets, 0, count, reinterpret_cast<jfloat*>(raw));
    }
    float window[4] = {};
    geode_wallpaper_window(surfaceW, surfaceH, displayW, displayH, raw, static_cast<int>(count), window);
    env->SetFloatArrayRegion(out, 0, 4, reinterpret_cast<const jfloat*>(window));
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizKnows(JNIEnv* env, jobject, jlong handle, jstring sceneId) {
    Utf8Chars id(env, sceneId);
    return geode_viz_knows(vizOf(handle), id.get()) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSceneIds(JNIEnv* env, jobject, jlong handle) {
    const size_t length = geode_viz_scene_ids(vizOf(handle), nullptr, 0);
    std::vector<char> buffer(length + 1, '\0');
    geode_viz_scene_ids(vizOf(handle), buffer.data(), buffer.size());
    return env->NewStringUTF(buffer.data());
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizLastError(JNIEnv* env, jobject, jlong handle) {
    return geode::jni::utf8String(env, geode_viz_last_error(vizOf(handle)));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSurfaceCreated(JNIEnv*, jobject, jlong handle) {
    geode_viz_surface_created(vizOf(handle));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSurfaceChanged(JNIEnv*, jobject, jlong handle, jint width, jint height) {
    geode_viz_surface_changed(vizOf(handle), width, height);
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSetScene(JNIEnv* env, jobject, jlong handle, jstring sceneId) {
    Utf8Chars id(env, sceneId);
    return geode_viz_set_scene(vizOf(handle), id.get()) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSetStyleSchedule(JNIEnv* env, jobject, jlong handle, jdoubleArray atMs,
                                                             jobjectArray sceneIds, jobjectArray milkPaths,
                                                             jobjectArray cutIds, jint everyBeats,
                                                             jobjectArray lockedIds) {
    const std::vector<std::string> scenes = utf8Strings(env, sceneIds);
    const std::vector<std::string> milks = utf8Strings(env, milkPaths);
    const std::vector<std::string> cut = utf8Strings(env, cutIds);
    const size_t timed = atMs ? static_cast<size_t>(env->GetArrayLength(atMs)) : 0;
    const size_t count = std::min(timed, std::min(scenes.size(), milks.size()));
    std::vector<double> times(count);
    if (count > 0) env->GetDoubleArrayRegion(atMs, 0, static_cast<jsize>(count), times.data());
    const std::vector<const char*> sceneChars = cStrings(scenes);
    const std::vector<const char*> milkChars = cStrings(milks);
    const std::vector<const char*> cutChars = cStrings(cut);
    const std::vector<std::string> locked = utf8Strings(env, lockedIds);
    const std::vector<const char*> lockedChars = cStrings(locked);
    geode_viz_set_style_schedule(vizOf(handle), times.data(), sceneChars.data(), milkChars.data(),
                                 static_cast<int>(count), cutChars.data(), static_cast<int>(cutChars.size()), everyBeats,
                                 lockedChars.data(), static_cast<int>(lockedChars.size()));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizWarmTransition(JNIEnv* env, jobject, jlong handle, jstring id) {
    Utf8Chars name(env, id);
    geode_viz_warm_transition(vizOf(handle), name.get());
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizCut(JNIEnv*, jobject, jlong handle) {
    geode_viz_cut(vizOf(handle));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizRender(JNIEnv*, jobject, jlong handle, jdouble timeSeconds, jint targetFbo) {
    geode_viz_render(vizOf(handle), timeSeconds, static_cast<uint32_t>(targetFbo));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizPrepareOffscreen(JNIEnv*, jobject, jlong handle) {
    geode_viz_prepare_offscreen(vizOf(handle));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizReleaseScenes(JNIEnv*, jobject, jlong handle) {
    geode_viz_release_scenes(vizOf(handle));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizTrimSpares(JNIEnv*, jobject, jlong handle) {
    geode_viz_trim_spares(vizOf(handle));
}

// pixels is a Java IntArray? (ARGB per Android's Bitmap.getPixels), so a null array is a valid "clear"
// call rather than a JNI error - handled before touching the (possibly null) array. width*height is
// checked against the array's actual length and treated as a
// clear when it doesn't fit, so a stale/mismatched width or height can never make
// geode_viz_set_underlay_rgba read past the JVM-owned buffer.
JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSetUnderlay(JNIEnv* env, jobject, jlong handle, jintArray pixels, jint width, jint height,
                                                        jint blend, jfloat amount) {
    if (!pixels) {
        geode_viz_set_underlay_rgba(vizOf(handle), nullptr, 0, 0, blend, amount);
        return;
    }
    jint* data = env->GetIntArrayElements(pixels, nullptr);
    if (!data) return;
    const jlong needed = static_cast<jlong>(width) * static_cast<jlong>(height);
    if (width > 0 && height > 0 && needed <= env->GetArrayLength(pixels)) {
        geode_viz_set_underlay_rgba(vizOf(handle), reinterpret_cast<const uint32_t*>(data), width, height, blend, amount);
    } else {
        geode_viz_set_underlay_rgba(vizOf(handle), nullptr, 0, 0, blend, amount);
    }
    env->ReleaseIntArrayElements(pixels, data, JNI_ABORT);
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_catalogueText(JNIEnv* env, jobject) {
    return env->NewStringUTF(geode_catalogue_text());
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_cataloguePresetsText(JNIEnv* env, jobject) {
    return env->NewStringUTF(geode_catalogue_presets_text());
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_paramsSchemaText(JNIEnv* env, jobject) {
    return env->NewStringUTF(geode_params_schema_text());
}

JNIEXPORT jfloat JNICALL
Java_dev_geode_engine_bridge_GeodeNative_paramsPaletteBase(JNIEnv*, jobject, jint palette, jfloat override) {
    return geode_params_palette_base(palette, override);
}

JNIEXPORT jfloat JNICALL
Java_dev_geode_engine_bridge_GeodeNative_paramsPaletteRange(JNIEnv*, jobject, jint palette, jfloat override) {
    return geode_params_palette_range(palette, override);
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_colourFromHsv(JNIEnv*, jobject, jfloat h, jfloat s, jfloat v) {
    return geode_colour_from_hsv(h, s, v);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_colourToHsv(JNIEnv* env, jobject, jint rgb, jfloatArray out) {
    if (!out || env->GetArrayLength(out) < 3) return;
    float hsv[3];
    geode_colour_to_hsv(rgb, hsv);
    env->SetFloatArrayRegion(out, 0, 3, hsv);
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_colourParseHex(JNIEnv* env, jobject, jstring text) {
    Utf8Chars chars(env, text);
    return geode_colour_parse_hex(chars.get());
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_colourHex(JNIEnv* env, jobject, jint rgb) {
    return env->NewStringUTF(geode_colour_hex(rgb));
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_colourAverageArgb(JNIEnv* env, jobject, jintArray argb) {
    if (!argb) return -1;
    const jsize n = env->GetArrayLength(argb);
    std::vector<int32_t> px(static_cast<size_t>(n));
    env->GetIntArrayRegion(argb, 0, n, reinterpret_cast<jint*>(px.data()));
    return geode_colour_average_argb(px.data(), n);
}

JNIEXPORT jfloatArray JNICALL
Java_dev_geode_engine_bridge_GeodeNative_artPaletteExtract(JNIEnv* env, jobject, jintArray argb) {
    if (!argb) return nullptr;
    const jsize n = env->GetArrayLength(argb);
    std::vector<int32_t> px(static_cast<size_t>(n));
    env->GetIntArrayRegion(argb, 0, n, reinterpret_cast<jint*>(px.data()));
    float got[3];
    if (!geode_art_palette_extract(px.data(), n, got)) return nullptr;
    jfloatArray out = env->NewFloatArray(3);
    if (out) env->SetFloatArrayRegion(out, 0, 3, got);
    return out;
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_paletteColourMapName(JNIEnv* env, jobject, jint index) {
    return env->NewStringUTF(geode_palette_colour_map_name(index));
}

JNIEXPORT jintArray JNICALL
Java_dev_geode_engine_bridge_GeodeNative_paletteRamp(JNIEnv* env, jobject, jintArray stops, jint samples) {
    const jsize n = stops ? std::min<jsize>(env->GetArrayLength(stops), 5) : 0;
    int in[5] = {-1, -1, -1, -1, -1};
    if (n > 0) env->GetIntArrayRegion(stops, 0, n, reinterpret_cast<jint*>(in));
    std::vector<int> out(static_cast<size_t>(std::clamp<jint>(samples, 0, kMaxSwatch)));
    geode_palette_ramp(in, n, static_cast<int>(out.size()), out.data());
    return toJava(env, out);
}

JNIEXPORT jintArray JNICALL
Java_dev_geode_engine_bridge_GeodeNative_paletteStopsFromHues(JNIEnv* env, jobject, jfloat base, jfloat range, jint count) {
    std::vector<int> out(static_cast<size_t>(std::clamp<jint>(count, 0, 5)));
    geode_palette_stops_from_hues(base, range, static_cast<int>(out.size()), out.data());
    return toJava(env, out);
}

JNIEXPORT jintArray JNICALL
Java_dev_geode_engine_bridge_GeodeNative_paramsPalettePreview(JNIEnv* env, jobject, jfloatArray params, jboolean second, jint samples) {
    std::vector<int> out(static_cast<size_t>(std::clamp<jint>(samples, 0, kMaxSwatch)));
    if (!params || env->GetArrayLength(params) < geode_viz_param_count()) return toJava(env, out);
    std::vector<float> wire(static_cast<size_t>(env->GetArrayLength(params)));
    env->GetFloatArrayRegion(params, 0, static_cast<jsize>(wire.size()), wire.data());
    geode_params_palette_preview(wire.data(), second ? 1 : 0, static_cast<int>(out.size()), out.data());
    return toJava(env, out);
}

JNIEXPORT jfloat JNICALL
Java_dev_geode_engine_bridge_GeodeNative_touchZoom(JNIEnv*, jobject, jfloat current, jfloat scaleFactor) {
    return geode_touch_zoom(current, scaleFactor);
}

JNIEXPORT jfloat JNICALL
Java_dev_geode_engine_bridge_GeodeNative_touchRotation(JNIEnv*, jobject, jfloat current, jfloat degrees) {
    return geode_touch_rotation(current, degrees);
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_customizeText(JNIEnv* env, jobject) {
    return env->NewStringUTF(geode_customize_text());
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_customizeLockableKeys(JNIEnv* env, jobject) {
    return env->NewStringUTF(geode_customize_lockable_keys());
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_customizeShown(JNIEnv* env, jobject, jstring styleId) {
    Utf8Chars style(env, styleId);
    return env->NewStringUTF(geode_customize_shown(style.get()));
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_customizeRandomize(JNIEnv* env, jobject, jstring styleId, jstring category, jstring locked,
                                                            jlong seed, jfloatArray in, jfloatArray out) {
    if (!in || !out || env->GetArrayLength(in) < geode_viz_param_count() || env->GetArrayLength(out) < geode_viz_param_count()) {
        return JNI_FALSE;
    }
    Utf8Chars style(env, styleId);
    Utf8Chars categoryName(env, category);
    Utf8Chars lockedKeys(env, locked);
    std::vector<float> input(static_cast<size_t>(geode_viz_param_count()));
    std::vector<float> output(input.size());
    env->GetFloatArrayRegion(in, 0, static_cast<jsize>(input.size()), input.data());
    const int changed = geode_customize_randomize(style.get(), categoryName.get(), lockedKeys.get(), static_cast<uint64_t>(seed),
                                                  input.data(), output.data());
    env->SetFloatArrayRegion(out, 0, static_cast<jsize>(output.size()), output.data());
    return changed ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_customizeReset(JNIEnv* env, jobject, jstring category, jfloatArray in, jfloatArray out) {
    if (!in || !out || env->GetArrayLength(in) < geode_viz_param_count() || env->GetArrayLength(out) < geode_viz_param_count()) return;
    Utf8Chars categoryName(env, category);
    std::vector<float> input(static_cast<size_t>(geode_viz_param_count()));
    std::vector<float> output(input.size());
    env->GetFloatArrayRegion(in, 0, static_cast<jsize>(input.size()), input.data());
    geode_customize_reset(categoryName.get(), input.data(), output.data());
    env->SetFloatArrayRegion(out, 0, static_cast<jsize>(output.size()), output.data());
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_customizeBorrow(JNIEnv* env, jobject, jstring category, jfloatArray in, jfloatArray from,
                                                         jfloatArray out) {
    const jsize count = static_cast<jsize>(geode_viz_param_count());
    if (!in || !from || !out || env->GetArrayLength(in) < count || env->GetArrayLength(from) < count || env->GetArrayLength(out) < count) {
        return;
    }
    Utf8Chars categoryName(env, category);
    std::vector<float> input(static_cast<size_t>(count));
    std::vector<float> source(input.size());
    std::vector<float> output(input.size());
    env->GetFloatArrayRegion(in, 0, count, input.data());
    env->GetFloatArrayRegion(from, 0, count, source.data());
    geode_customize_borrow(categoryName.get(), input.data(), source.data(), output.data());
    env->SetFloatArrayRegion(out, 0, count, output.data());
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_randomizeAllPick(JNIEnv* env, jobject, jlong seed, jstring current, jstring lockedStyles,
                                                          jstring lookStyles, jint milkCount, jintArray picks) {
    if (!picks || env->GetArrayLength(picks) < 2) return nullptr;
    Utf8Chars style(env, current);
    Utf8Chars locked(env, lockedStyles);
    Utf8Chars looks(env, lookStyles);
    char picked[128];
    int look = -1;
    int milk = -1;
    const int ok = geode_randomize_all_pick(static_cast<uint64_t>(seed), style.get(), locked.get(), looks.get(), milkCount, picked,
                                            static_cast<int>(sizeof picked), &look, &milk);
    const jint out[2] = {look, milk};
    env->SetIntArrayRegion(picks, 0, 2, out);
    return ok ? env->NewStringUTF(picked) : nullptr;
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_randomizeAllRoll(JNIEnv* env, jobject, jstring styleId, jstring locked, jlong seed,
                                                          jboolean rollCategories, jfloatArray before, jfloatArray base,
                                                          jfloatArray out) {
    const jsize count = static_cast<jsize>(geode_viz_param_count());
    if (!before || !base || !out || env->GetArrayLength(before) < count || env->GetArrayLength(base) < count ||
        env->GetArrayLength(out) < count) {
        return JNI_FALSE;
    }
    Utf8Chars style(env, styleId);
    Utf8Chars lockedKeys(env, locked);
    std::vector<float> was(static_cast<size_t>(count));
    std::vector<float> start(was.size());
    std::vector<float> rolled(was.size());
    env->GetFloatArrayRegion(before, 0, count, was.data());
    env->GetFloatArrayRegion(base, 0, count, start.data());
    const int changed =
        geode_randomize_all_roll(style.get(), lockedKeys.get(), static_cast<uint64_t>(seed), rollCategories ? 1 : 0, was.data(),
                                 start.data(), rolled.data());
    env->SetFloatArrayRegion(out, 0, count, rolled.data());
    return changed ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_configSchemaText(JNIEnv* env, jobject) {
    return env->NewStringUTF(geode_config_schema_text());
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_paramsRollKeys(JNIEnv* env, jobject) {
    return env->NewStringUTF(geode_params_roll_keys());
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_paramsRandomize(JNIEnv* env, jobject, jstring styleId, jstring tab, jstring locked,
                                                         jlong seed, jfloatArray in, jfloatArray out) {
    if (!in || !out || env->GetArrayLength(in) < geode_viz_param_count() || env->GetArrayLength(out) < geode_viz_param_count()) {
        return JNI_FALSE;
    }
    Utf8Chars style(env, styleId);
    Utf8Chars tabName(env, tab);
    Utf8Chars lockedKeys(env, locked);
    std::vector<float> input(static_cast<size_t>(geode_viz_param_count()));
    std::vector<float> output(input.size());
    env->GetFloatArrayRegion(in, 0, static_cast<jsize>(input.size()), input.data());
    const int changed = geode_params_randomize(style.get(), tabName.get(), lockedKeys.get(), static_cast<uint64_t>(seed),
                                               input.data(), output.data());
    env->SetFloatArrayRegion(out, 0, static_cast<jsize>(output.size()), output.data());
    return changed ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_wavesRandomize(JNIEnv* env, jobject, jstring styleId, jstring locked, jlong seed,
                                                        jboolean runningOnly, jfloatArray params, jfloatArray in, jfloatArray out) {
    constexpr jsize kWaves = GEODE_LFO_SLOTS * GEODE_LFO_CONFIG_FLOATS;
    if (!params || !in || !out || env->GetArrayLength(params) < geode_viz_param_count() || env->GetArrayLength(in) < kWaves ||
        env->GetArrayLength(out) < kWaves) {
        return JNI_FALSE;
    }
    Utf8Chars style(env, styleId);
    Utf8Chars lockedKeys(env, locked);
    std::vector<float> look(static_cast<size_t>(geode_viz_param_count()));
    float input[kWaves];
    float output[kWaves];
    env->GetFloatArrayRegion(params, 0, static_cast<jsize>(look.size()), look.data());
    env->GetFloatArrayRegion(in, 0, kWaves, input);
    const int changed = geode_waves_randomize(style.get(), lockedKeys.get(), static_cast<uint64_t>(seed), runningOnly ? 1 : 0,
                                              look.data(), input, output);
    env->SetFloatArrayRegion(out, 0, kWaves, output);
    return changed ? JNI_TRUE : JNI_FALSE;
}

// Fixed-rate frame clock (Cast): the state is a caller-owned long[GEODE_FRAME_CLOCK_STATE], copied in and out.
JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_frameClockStart(JNIEnv* env, jobject, jlongArray state, jint fps, jlong nowNs) {
    if (!state || env->GetArrayLength(state) < GEODE_FRAME_CLOCK_STATE) return;
    int64_t s[GEODE_FRAME_CLOCK_STATE] = {};
    geode_frame_clock_start(s, fps, nowNs);
    env->SetLongArrayRegion(state, 0, GEODE_FRAME_CLOCK_STATE, reinterpret_cast<const jlong*>(s));
}

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_frameClockWaitNs(JNIEnv* env, jobject, jlongArray state, jlong nowNs) {
    if (!state || env->GetArrayLength(state) < GEODE_FRAME_CLOCK_STATE) return 0;
    int64_t s[GEODE_FRAME_CLOCK_STATE];
    env->GetLongArrayRegion(state, 0, GEODE_FRAME_CLOCK_STATE, reinterpret_cast<jlong*>(s));
    return geode_frame_clock_wait_ns(s, nowNs);
}

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_frameClockTake(JNIEnv* env, jobject, jlongArray state, jlong nowNs) {
    if (!state || env->GetArrayLength(state) < GEODE_FRAME_CLOCK_STATE) return 0;
    int64_t s[GEODE_FRAME_CLOCK_STATE];
    env->GetLongArrayRegion(state, 0, GEODE_FRAME_CLOCK_STATE, reinterpret_cast<jlong*>(s));
    const int64_t pts = geode_frame_clock_take(s, nowNs);
    env->SetLongArrayRegion(state, 0, GEODE_FRAME_CLOCK_STATE, reinterpret_cast<const jlong*>(s));
    return pts;
}

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_frameClockAudioUntilUs(JNIEnv* env, jobject, jlongArray state) {
    if (!state || env->GetArrayLength(state) < GEODE_FRAME_CLOCK_STATE) return 0;
    int64_t s[GEODE_FRAME_CLOCK_STATE];
    env->GetLongArrayRegion(state, 0, GEODE_FRAME_CLOCK_STATE, reinterpret_cast<jlong*>(s));
    return geode_frame_clock_audio_until_us(s);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_castFormat(JNIEnv* env, jobject, jintArray out) {
    if (!out || env->GetArrayLength(out) < GEODE_CAST_FORMAT_INTS) return;
    int f[GEODE_CAST_FORMAT_INTS];
    geode_cast_format(f);
    env->SetIntArrayRegion(out, 0, GEODE_CAST_FORMAT_INTS, reinterpret_cast<const jint*>(f));
}

// Export post-passes (core/viz/frame/ExportPasses): marshalling only.

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_exportPassError(JNIEnv* env, jobject, jlong handle) {
    return geode::jni::utf8String(env, geode_export_pass_error(passOf(handle)));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_exportPassDestroy(JNIEnv*, jobject, jlong handle) {
    geode_export_pass_destroy(passOf(handle));
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_exportWatermarkPlacement(JNIEnv*, jobject, jboolean required, jboolean proAdds,
                                                                 jint placement, jint use) {
    return geode_export_watermark_placement(required ? 1 : 0, proAdds ? 1 : 0, placement, use);
}

JNIEXPORT jfloat JNICALL
Java_dev_geode_engine_bridge_GeodeNative_exportWatermarkTextPx(JNIEnv*, jobject, jint shortSide, jint placement) {
    return geode_export_watermark_text_px(shortSide, placement);
}

JNIEXPORT jfloat JNICALL
Java_dev_geode_engine_bridge_GeodeNative_exportWatermarkAngle(JNIEnv*, jobject, jint placement) {
    return geode_export_watermark_angle(placement);
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_exportWatermarkAlpha(JNIEnv*, jobject, jint placement) {
    return geode_export_watermark_alpha(placement);
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_exportWatermarkTileWidth(JNIEnv*, jobject, jfloat textWidth, jfloat textPx, jint placement) {
    return geode_export_watermark_tile_width(textWidth, textPx, placement);
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_exportWatermarkTileHeight(JNIEnv*, jobject, jint tileWidth, jfloat textPx, jint placement) {
    return geode_export_watermark_tile_height(tileWidth, textPx, placement);
}

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_exportWatermarkCreate(JNIEnv* env, jobject, jobject assetManager, jobject rgba,
                                                              jint tileWidth, jint tileHeight, jint placement, jint corner,
                                                              jlong lengthMs) {
    const uint8_t* pixels = directBytes(env, rgba, rgbaBytes(tileWidth, tileHeight));
    return reinterpret_cast<jlong>(geode_export_watermark_create(assetsOf(env, assetManager), pixels, tileWidth, tileHeight,
                                                                 placement, corner, lengthMs));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_exportWatermarkDraw(JNIEnv*, jobject, jlong handle, jint frameWidth, jint frameHeight,
                                                            jlong timeMs) {
    geode_export_watermark_draw(passOf(handle), frameWidth, frameHeight, timeMs);
}

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_exportSeamCreate(JNIEnv* env, jobject, jobject assetManager, jint width, jint height) {
    return reinterpret_cast<jlong>(geode_export_seam_create(assetsOf(env, assetManager), width, height));
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_exportSeamAllocate(JNIEnv*, jobject, jlong handle, jint requested) {
    return geode_export_seam_allocate(passOf(handle), requested);
}

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_exportSeamFrame(JNIEnv*, jobject, jlong handle, jint frame, jint loopFrames,
                                                        jint seamFrames, jint fps) {
    return geode_export_seam_frame(passOf(handle), frame, loopFrames, seamFrames, fps);
}

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_exportSeamBudgetedCrossfadeMs(JNIEnv*, jobject, jlong crossfadeMs,
                                                                      jint crossfadeFrames, jint fps, jint width,
                                                                      jint height) {
    return geode_export_seam_budgeted_crossfade_ms(crossfadeMs, crossfadeFrames, fps, width, height);
}

JNIEXPORT jfloat JNICALL
Java_dev_geode_engine_bridge_GeodeNative_exportLoopProgress(JNIEnv*, jobject, jint stop, jint stops, jint frame,
                                                           jint loopFrames, jint seamFrames) {
    return geode_export_loop_progress(stop, stops, frame, loopFrames, seamFrames);
}

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_exportLoopReportedCrossfadeMs(JNIEnv*, jobject, jint seamFrames, jint fps) {
    return geode_export_loop_reported_crossfade_ms(seamFrames, fps);
}

JNIEXPORT jbyteArray JNICALL
Java_dev_geode_engine_bridge_GeodeNative_transitionsTable(JNIEnv* env, jobject, jobject assetManager) {
    // Bytes, not a jstring: NewStringUTF takes modified UTF-8, and the library's text is plain UTF-8.
    AAssetManager* assets = assetManager ? AAssetManager_fromJava(env, assetManager) : nullptr;
    const char* table = geode_transitions_table(assets);
    const auto n = static_cast<jsize>(std::strlen(table));
    jbyteArray out = env->NewByteArray(n);
    if (out) env->SetByteArrayRegion(out, 0, n, reinterpret_cast<const jbyte*>(table));
    return out;
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_perfTier(JNIEnv* env, jobject, jint mode, jfloatArray out) {
    if (!out || env->GetArrayLength(out) < GEODE_PERF_TIER_FLOATS) return JNI_FALSE;
    float values[GEODE_PERF_TIER_FLOATS];
    if (geode_perf_tier(mode, values, GEODE_PERF_TIER_FLOATS) != GEODE_PERF_TIER_FLOATS) return JNI_FALSE;
    env->SetFloatArrayRegion(out, 0, GEODE_PERF_TIER_FLOATS, values);
    return JNI_TRUE;
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_liveFpsChoices(JNIEnv* env, jobject, jfloatArray rates, jintArray out) {
    if (!out) return 0;
    FloatElements r(env, rates);
    int choices[GEODE_LIVE_FPS_MAX_CHOICES];
    const int n = geode_live_fps_choices(r.get(), r.length(), choices, GEODE_LIVE_FPS_MAX_CHOICES);
    const jsize written = std::min<jsize>({static_cast<jsize>(n), env->GetArrayLength(out), GEODE_LIVE_FPS_MAX_CHOICES});
    if (written > 0) env->SetIntArrayRegion(out, 0, written, reinterpret_cast<const jint*>(choices));
    return n;
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_liveFpsCap(JNIEnv* env, jobject, jint storedFps, jfloatArray rates) {
    FloatElements r(env, rates);
    return geode_live_fps_cap(storedFps, r.get(), r.length());
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_perfModeForDevice(JNIEnv*, jobject, jlong totalRamBytes, jboolean lowRam,
                                                          jint mediaPerformanceClass) {
    return geode_perf_mode_for_device(totalRamBytes, lowRam ? 1 : 0, mediaPerformanceClass);
}

JNIEXPORT jdouble JNICALL
Java_dev_geode_engine_bridge_GeodeNative_pacerOnVsync(JNIEnv* env, jobject, jlongArray state, jlong frameTimeNanos,
                                                     jfloat capFps) {
    if (!state || env->GetArrayLength(state) < GEODE_PACER_STATE_LONGS) return 0.0;
    long long wire[GEODE_PACER_STATE_LONGS];
    env->GetLongArrayRegion(state, 0, GEODE_PACER_STATE_LONGS, reinterpret_cast<jlong*>(wire));
    const double dt = geode_pacer_on_vsync(wire, frameTimeNanos, capFps);
    env->SetLongArrayRegion(state, 0, GEODE_PACER_STATE_LONGS, reinterpret_cast<const jlong*>(wire));
    return dt;
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_pacerResume(JNIEnv* env, jobject, jlongArray state) {
    if (!state || env->GetArrayLength(state) < GEODE_PACER_STATE_LONGS) return;
    long long wire[GEODE_PACER_STATE_LONGS];
    env->GetLongArrayRegion(state, 0, GEODE_PACER_STATE_LONGS, reinterpret_cast<jlong*>(wire));
    geode_pacer_resume(wire);
    env->SetLongArrayRegion(state, 0, GEODE_PACER_STATE_LONGS, reinterpret_cast<const jlong*>(wire));
}

// The background image bake (core/viz/Underlay). The rect comes back as a new int[4] (x, y, width, height).
JNIEXPORT jintArray JNICALL
Java_dev_geode_engine_bridge_GeodeNative_underlayCropRect(JNIEnv* env, jobject, jint srcW, jint srcH, jint targetW, jint targetH) {
    jint rect[4] = {0, 0, 0, 0};
    geode_underlay_crop_rect(srcW, srcH, targetW, targetH, rect);
    jintArray result = env->NewIntArray(4);
    if (result) env->SetIntArrayRegion(result, 0, 4, rect);
    return result;
}

// In place on width*height ARGB ints; a size the array cannot hold does nothing.
JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_underlayFinish(JNIEnv* env, jobject, jintArray pixels, jint width, jint height,
                                                        jint blurRadius, jfloat dim) {
    if (!pixels || width <= 0 || height <= 0) return;
    const jlong count = static_cast<jlong>(width) * static_cast<jlong>(height);
    if (count > env->GetArrayLength(pixels)) return;
    std::vector<uint32_t> data(static_cast<size_t>(count));
    env->GetIntArrayRegion(pixels, 0, static_cast<jsize>(count), reinterpret_cast<jint*>(data.data()));
    geode_underlay_finish(data.data(), width, height, blurRadius, dim);
    env->SetIntArrayRegion(pixels, 0, static_cast<jsize>(count), reinterpret_cast<const jint*>(data.data()));
}

JNIEXPORT jfloat JNICALL
Java_dev_geode_engine_bridge_GeodeNative_underlayClampAmount(JNIEnv*, jobject, jfloat amount) {
    return geode_underlay_clamp_amount(amount);
}

JNIEXPORT jfloat JNICALL
Java_dev_geode_engine_bridge_GeodeNative_underlayClampDim(JNIEnv*, jobject, jfloat dim) {
    return geode_underlay_clamp_dim(dim);
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_underlayClampBlur(JNIEnv*, jobject, jint radius) {
    return geode_underlay_clamp_blur(radius);
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_underlayBlurMax(JNIEnv*, jobject) {
    return geode_underlay_blur_max();
}

JNIEXPORT jfloat JNICALL
Java_dev_geode_engine_bridge_GeodeNative_pacerEffectiveCap(JNIEnv*, jobject, jfloat capFps) {
    return geode_pacer_effective_cap(capFps);
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_thermalTakeWarmNote(JNIEnv*, jobject, jfloat capFps, jfloat pacedFps) {
    return geode_thermal_take_warm_note(capFps, pacedFps);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_memoryTrim(JNIEnv*, jobject, jint level) {
    geode_memory_trim(level);
}

// Cast's audio pump: the state is a caller-owned long[GEODE_CAST_AUDIO_STATE], a step's result a long[GEODE_CAST_AUDIO_STEP].
JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_castAudioStart(JNIEnv* env, jobject, jlongArray state, jint rateHz) {
    if (!state || env->GetArrayLength(state) < GEODE_CAST_AUDIO_STATE) return 0;
    int64_t s[GEODE_CAST_AUDIO_STATE] = {};
    const int capacity = geode_cast_audio_start(s, rateHz);
    env->SetLongArrayRegion(state, 0, GEODE_CAST_AUDIO_STATE, reinterpret_cast<const jlong*>(s));
    return capacity;
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_castAudioStep(JNIEnv* env, jobject, jlongArray state, jint gainedFrames,
                                                       jlong untilUs, jlongArray out) {
    if (!state || !out || env->GetArrayLength(state) < GEODE_CAST_AUDIO_STATE ||
        env->GetArrayLength(out) < GEODE_CAST_AUDIO_STEP) return;
    int64_t s[GEODE_CAST_AUDIO_STATE];
    int64_t o[GEODE_CAST_AUDIO_STEP] = {};
    env->GetLongArrayRegion(state, 0, GEODE_CAST_AUDIO_STATE, reinterpret_cast<jlong*>(s));
    geode_cast_audio_step(s, gainedFrames, untilUs, o);
    env->SetLongArrayRegion(state, 0, GEODE_CAST_AUDIO_STATE, reinterpret_cast<const jlong*>(s));
    env->SetLongArrayRegion(out, 0, GEODE_CAST_AUDIO_STEP, reinterpret_cast<const jlong*>(o));
}

// The AAC encoder's backlog (core/viz/frame/CastAudio, CastAudioFeed): a caller-owned long[GEODE_CAST_AUDIO_FEED_STATE].
JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_castAudioFeedStart(JNIEnv* env, jobject, jlongArray state, jint rateHz) {
    if (!state || env->GetArrayLength(state) < GEODE_CAST_AUDIO_FEED_STATE) return 0;
    int64_t s[GEODE_CAST_AUDIO_FEED_STATE] = {};
    const int capacity = geode_cast_audio_feed_start(s, rateHz);
    env->SetLongArrayRegion(state, 0, GEODE_CAST_AUDIO_FEED_STATE, reinterpret_cast<const jlong*>(s));
    return capacity;
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_castAudioFeedAdd(JNIEnv* env, jobject, jlongArray state, jint frames) {
    if (!state || env->GetArrayLength(state) < GEODE_CAST_AUDIO_FEED_STATE) return 0;
    int64_t s[GEODE_CAST_AUDIO_FEED_STATE];
    env->GetLongArrayRegion(state, 0, GEODE_CAST_AUDIO_FEED_STATE, reinterpret_cast<jlong*>(s));
    const int64_t drop = geode_cast_audio_feed_add(s, frames);
    env->SetLongArrayRegion(state, 0, GEODE_CAST_AUDIO_FEED_STATE, reinterpret_cast<const jlong*>(s));
    return static_cast<jint>(drop);
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_castAudioFeedTake(JNIEnv* env, jobject, jlongArray state, jint bufferFrames,
                                                           jlongArray ptsUs) {
    if (!state || !ptsUs || env->GetArrayLength(state) < GEODE_CAST_AUDIO_FEED_STATE || env->GetArrayLength(ptsUs) < 1) {
        return 0;
    }
    int64_t s[GEODE_CAST_AUDIO_FEED_STATE];
    int64_t pts = 0;
    env->GetLongArrayRegion(state, 0, GEODE_CAST_AUDIO_FEED_STATE, reinterpret_cast<jlong*>(s));
    const int64_t n = geode_cast_audio_feed_take(s, bufferFrames, &pts);
    env->SetLongArrayRegion(state, 0, GEODE_CAST_AUDIO_FEED_STATE, reinterpret_cast<const jlong*>(s));
    env->SetLongArrayRegion(ptsUs, 0, 1, reinterpret_cast<const jlong*>(&pts));
    return static_cast<jint>(n);
}

// Cast's HLS timing (core/viz/frame/HlsSegments): the init segment's and a fragment's bytes in, its media seconds out.
JNIEXPORT jdouble JNICALL
Java_dev_geode_engine_bridge_GeodeNative_hlsFragmentSeconds(JNIEnv* env, jobject, jbyteArray init, jbyteArray fragment) {
    if (!init || !fragment) return 0.0;
    const jsize initSize = env->GetArrayLength(init);
    const jsize fragmentSize = env->GetArrayLength(fragment);
    auto* i = static_cast<const uint8_t*>(env->GetPrimitiveArrayCritical(init, nullptr));
    auto* f = static_cast<const uint8_t*>(env->GetPrimitiveArrayCritical(fragment, nullptr));
    const double seconds = (i && f) ? geode_hls_fragment_seconds(i, static_cast<size_t>(initSize), f, static_cast<size_t>(fragmentSize)) : 0.0;
    if (f) env->ReleasePrimitiveArrayCritical(fragment, const_cast<uint8_t*>(f), JNI_ABORT);
    if (i) env->ReleasePrimitiveArrayCritical(init, const_cast<uint8_t*>(i), JNI_ABORT);
    return seconds;
}

// A fragment rewritten into a self-contained segment; the decode times are a caller-owned long[GEODE_HLS_TRACKS].
JNIEXPORT jbyteArray JNICALL
Java_dev_geode_engine_bridge_GeodeNative_hlsRewriteFragment(JNIEnv* env, jobject, jbyteArray init, jlong moofAt,
                                                            jbyteArray fragment, jlongArray decodeTimes) {
    if (!init || !fragment || !decodeTimes || env->GetArrayLength(decodeTimes) < GEODE_HLS_TRACKS) return nullptr;
    std::vector<uint8_t> i(static_cast<size_t>(env->GetArrayLength(init)));
    std::vector<uint8_t> f(static_cast<size_t>(env->GetArrayLength(fragment)));
    env->GetByteArrayRegion(init, 0, static_cast<jsize>(i.size()), reinterpret_cast<jbyte*>(i.data()));
    env->GetByteArrayRegion(fragment, 0, static_cast<jsize>(f.size()), reinterpret_cast<jbyte*>(f.data()));
    int64_t times[GEODE_HLS_TRACKS];
    env->GetLongArrayRegion(decodeTimes, 0, GEODE_HLS_TRACKS, reinterpret_cast<jlong*>(times));
    std::vector<uint8_t> out(f.size() + GEODE_HLS_FRAGMENT_GROWTH);
    const size_t n = geode_hls_rewrite_fragment(i.data(), i.size(), moofAt, f.data(), f.size(), times, out.data(), out.size());
    if (n == 0) return nullptr;
    env->SetLongArrayRegion(decodeTimes, 0, GEODE_HLS_TRACKS, reinterpret_cast<const jlong*>(times));
    jbyteArray result = env->NewByteArray(static_cast<jsize>(n));
    if (result) env->SetByteArrayRegion(result, 0, static_cast<jsize>(n), reinterpret_cast<const jbyte*>(out.data()));
    return result;
}

// Show on a computer (core/viz/frame/LinkShare, LinkPage) and its QR code (core/util/QrCode): marshalling only.
JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_linkRoute(JNIEnv* env, jobject, jstring method, jstring target, jstring token,
                                                   jlongArray out) {
    if (!out || env->GetArrayLength(out) < 2) return 404;
    geode::jni::Utf8Chars m(env, method), t(env, target), k(env, token);
    int resource = GEODE_LINK_NONE;
    int64_t sequence = -1;
    const int status = geode_link_route(m.get(), t.get(), k.get(), &resource, &sequence);
    const jlong values[2] = {resource, sequence};
    env->SetLongArrayRegion(out, 0, 2, values);
    return status;
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_linkResponseHead(JNIEnv* env, jobject, jint status, jint resource, jlong length,
                                                          jstring token) {
    geode::jni::Utf8Chars k(env, token);
    std::vector<char> text(static_cast<size_t>(geode_link_response_head(status, resource, length, k.get(), nullptr, 0)) + 1);
    geode_link_response_head(status, resource, length, k.get(), text.data(), static_cast<int>(text.size()));
    return geode::jni::utf8String(env, text.data());
}

JNIEXPORT jbyteArray JNICALL
Java_dev_geode_engine_bridge_GeodeNative_linkAsset(JNIEnv* env, jobject, jint resource) {
    size_t size = 0;
    const char* data = geode_link_asset(resource, &size);
    if (!data) return nullptr;
    jbyteArray out = env->NewByteArray(static_cast<jsize>(size));
    if (out) env->SetByteArrayRegion(out, 0, static_cast<jsize>(size), reinterpret_cast<const jbyte*>(data));
    return out;
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_linkMasterPlaylist(JNIEnv* env, jobject, jbyteArray init) {
    if (!init) return nullptr;
    std::vector<uint8_t> i(static_cast<size_t>(env->GetArrayLength(init)));
    env->GetByteArrayRegion(init, 0, static_cast<jsize>(i.size()), reinterpret_cast<jbyte*>(i.data()));
    std::vector<char> text(static_cast<size_t>(geode_link_master_playlist(i.data(), i.size(), nullptr, 0)) + 1);
    return geode_link_master_playlist(i.data(), i.size(), text.data(), static_cast<int>(text.size())) > 0
               ? geode::jni::utf8String(env, text.data())
               : nullptr;
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_linkStep(JNIEnv* env, jobject, jintArray state, jint event, jint arg) {
    if (!state || env->GetArrayLength(state) < GEODE_LINK_STATE) return 0;
    int s[GEODE_LINK_STATE];
    env->GetIntArrayRegion(state, 0, GEODE_LINK_STATE, reinterpret_cast<jint*>(s));
    const int actions = geode_link_step(s, event, arg);
    env->SetIntArrayRegion(state, 0, GEODE_LINK_STATE, reinterpret_cast<const jint*>(s));
    return actions;
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_linkAdmit(JNIEnv*, jobject, jint peer, jint bound, jint prefix, jint tv, jint wants) {
    return geode_link_admit(static_cast<uint32_t>(peer), static_cast<uint32_t>(bound), prefix, static_cast<uint32_t>(tv), wants);
}

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_linkBodyLength(JNIEnv* env, jobject, jobjectArray headers, jlong max) {
    const jsize n = headers ? env->GetArrayLength(headers) : 0;
    std::vector<std::string> lines;
    lines.reserve(static_cast<size_t>(n));
    for (jsize i = 0; i < n; ++i) {
        auto line = static_cast<jstring>(env->GetObjectArrayElement(headers, i));
        geode::jni::Utf8Chars c(env, line);
        lines.emplace_back(c.get() ? c.get() : "");
        if (line) env->DeleteLocalRef(line);
    }
    std::vector<const char*> pointers;
    pointers.reserve(lines.size());
    for (const std::string& l : lines) pointers.push_back(l.c_str());
    return geode_link_body_length(pointers.data(), static_cast<int>(pointers.size()), max);
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_linkRtcStatus(JNIEnv*, jobject, jint answerResult) {
    return geode_link_rtc_status(answerResult);
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_linkShareUrl(JNIEnv* env, jobject, jint address, jint port, jstring token) {
    geode::jni::Utf8Chars k(env, token);
    std::vector<char> text(static_cast<size_t>(geode_link_share_url(static_cast<uint32_t>(address), port, k.get(), nullptr, 0)) + 1);
    return geode_link_share_url(static_cast<uint32_t>(address), port, k.get(), text.data(), static_cast<int>(text.size())) > 0
               ? geode::jni::utf8String(env, text.data())
               : nullptr;
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_linkLabel(JNIEnv* env, jobject, jstring token) {
    geode::jni::Utf8Chars k(env, token);
    std::vector<char> text(static_cast<size_t>(geode_link_label(k.get(), nullptr, 0)) + 1);
    geode_link_label(k.get(), text.data(), static_cast<int>(text.size()));
    return geode::jni::utf8String(env, text.data());
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_linkReceiverHealth(JNIEnv*, jobject, jlong nowMs, jlong loadMs, jlong lastFetchMs,
                                                            jint playerState, jint idleReason) {
    return geode_link_receiver_health(nowMs, loadMs, lastFetchMs, playerState, idleReason);
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_qrEncode(JNIEnv* env, jobject, jstring text, jbyteArray out) {
    if (!out || env->GetArrayLength(out) < GEODE_QR_MAX_MODULES) return 0;
    geode::jni::Utf8Chars t(env, text);
    if (!t.get()) return 0;
    const size_t n = std::strlen(t.get());
    std::vector<uint8_t> modules(GEODE_QR_MAX_MODULES);
    const int side = geode_qr_encode(reinterpret_cast<const uint8_t*>(t.get()), static_cast<int>(n), modules.data(),
                                     static_cast<int>(modules.size()));
    if (side > 0) env->SetByteArrayRegion(out, 0, side * side, reinterpret_cast<const jbyte*>(modules.data()));
    return side;
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_hlsWindow(JNIEnv*, jobject) {
    return geode_hls_window();
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_hlsPlaylist(JNIEnv* env, jobject, jlong firstSequence, jdoubleArray durations,
                                                     jstring initUri, jstring segmentPrefix, jstring segmentSuffix) {
    if (!durations) return nullptr;
    std::vector<double> d(static_cast<size_t>(env->GetArrayLength(durations)));
    env->GetDoubleArrayRegion(durations, 0, static_cast<jsize>(d.size()), d.data());
    geode::jni::Utf8Chars uri(env, initUri), prefix(env, segmentPrefix), suffix(env, segmentSuffix);
    const auto write = [&](char* out, int cap) {
        return geode_hls_playlist(firstSequence, d.data(), static_cast<int>(d.size()), uri.get(), prefix.get(),
                                  suffix.get(), out, cap);
    };
    std::vector<char> text(static_cast<size_t>(write(nullptr, 0)) + 1);
    return write(text.data(), static_cast<int>(text.size())) > 0 ? geode::jni::utf8String(env, text.data()) : nullptr;
}

// Record (output/Recorder, viz/frame/RecordTap): a live video of the visual and its sound.

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_recordNowNs(JNIEnv*, jobject) {
    return geode_record_now_ns();
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_recorderCreate(JNIEnv*, jobject, jint stageWidth,
                                                                                 jint stageHeight, jint fps, jint maxSec,
                                                                                 jlong capMs) {
    return reinterpret_cast<jlong>(geode_recorder_create(stageWidth, stageHeight, fps, maxSec, capMs));
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_recorderDestroy(JNIEnv*, jobject, jlong handle) {
    geode_recorder_destroy(reinterpret_cast<geode_recorder*>(handle));
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_recorderStop(JNIEnv*, jobject, jlong handle, jint why,
                                                                              jlong nowNs) {
    geode_recorder_stop(reinterpret_cast<geode_recorder*>(handle), why, nowNs);
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_GeodeNative_recorderStopped(JNIEnv*, jobject, jlong handle) {
    return geode_recorder_stopped(reinterpret_cast<const geode_recorder*>(handle));
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_recorderRecordedMs(JNIEnv*, jobject, jlong handle,
                                                                                     jlong nowNs) {
    return geode_recorder_recorded_ms(reinterpret_cast<const geode_recorder*>(handle), nowNs);
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_recorderFramesDropped(JNIEnv*, jobject, jlong handle) {
    return geode_recorder_frames_dropped(reinterpret_cast<const geode_recorder*>(handle));
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_recorderLimitMs(JNIEnv*, jobject, jlong handle) {
    return geode_recorder_limit_ms(reinterpret_cast<const geode_recorder*>(handle));
}

JNIEXPORT jboolean JNICALL Java_dev_geode_engine_bridge_GeodeNative_recordWorthKeeping(JNIEnv*, jobject, jlong recordedMs) {
    return geode_record_worth_keeping(recordedMs) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_recordSessionOpen(JNIEnv*, jobject, jlong recorder,
                                                                                   jint fd, jint stageWidth,
                                                                                   jint stageHeight, jint shortSide,
                                                                                   jint fps) {
    return reinterpret_cast<jlong>(geode_record_session_open(reinterpret_cast<const geode_recorder*>(recorder), fd,
                                                             stageWidth, stageHeight, shortSide, fps));
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_recordSessionDestroy(JNIEnv*, jobject, jlong session) {
    geode_record_session_destroy(reinterpret_cast<geode_record_session*>(session));
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_recordSessionSize(JNIEnv* env, jobject, jlong session,
                                                                                  jintArray out) {
    if (!out || env->GetArrayLength(out) < 2) return;
    int width = 0;
    int height = 0;
    geode_record_session_size(reinterpret_cast<const geode_record_session*>(session), &width, &height);
    const jint size[2] = {width, height};
    env->SetIntArrayRegion(out, 0, 2, size);
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_GeodeNative_recordSessionState(JNIEnv*, jobject, jlong session) {
    return geode_record_session_state(reinterpret_cast<const geode_record_session*>(session));
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_recordSessionTeeClosed(JNIEnv*, jobject,
                                                                                       jlong session) {
    geode_record_session_tee_closed(reinterpret_cast<geode_record_session*>(session));
}

JNIEXPORT jboolean JNICALL Java_dev_geode_engine_bridge_GeodeNative_recordSessionTeeStart(JNIEnv*, jobject, jlong session,
                                                                                         jlong handle,
                                                                                         jlong watermark) {
    return geode_record_session_tee_start(reinterpret_cast<geode_record_session*>(session), vizOf(handle),
                                          passOf(watermark))
               ? JNI_TRUE
               : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_vizRecordStop(JNIEnv*, jobject, jlong handle) {
    geode_viz_record_stop(vizOf(handle));
}

// Cast's tee (viz/frame/CastTee): the stage's picture to the cast stream.

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_vizSetCastSource(JNIEnv*, jobject, jlong handle,
                                                                                  jboolean on) {
    geode_viz_set_cast_source(vizOf(handle), on ? 1 : 0);
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_castTeeOpen(JNIEnv*, jobject, jint width, jint height,
                                                                              jint fps) {
    return reinterpret_cast<jlong>(geode_cast_tee_open(width, height, fps));
}

JNIEXPORT jboolean JNICALL Java_dev_geode_engine_bridge_GeodeNative_castTeeDraw(JNIEnv*, jobject, jlong tee, jlong nowNs,
                                                                                 jint width, jint height) {
    return geode_cast_tee_draw(reinterpret_cast<geode_cast_tee*>(tee), nowNs, width, height) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_castTeeClose(JNIEnv*, jobject, jlong tee) {
    geode_cast_tee_close(reinterpret_cast<geode_cast_tee*>(tee));
}

JNIEXPORT jboolean JNICALL Java_dev_geode_engine_bridge_GeodeNative_castTeeAwaitStage(JNIEnv*, jobject, jboolean stageDraws,
                                                                                       jboolean fallbackMade,
                                                                                       jlong sinceOpenNs) {
    return geode_cast_tee_await_stage(stageDraws ? 1 : 0, fallbackMade ? 1 : 0, sinceOpenNs) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_vizStillRequest(JNIEnv*, jobject, jlong handle) {
    geode_viz_still_request(vizOf(handle));
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizStillSize(JNIEnv* env, jobject, jlong handle, jintArray out) {
    if (!out || env->GetArrayLength(out) < 2) return JNI_FALSE;
    jint size[2] = {0, 0};
    if (!geode_viz_still_size(vizOf(handle), &size[0], &size[1])) return JNI_FALSE;
    env->SetIntArrayRegion(out, 0, 2, size);
    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizStillRead(JNIEnv* env, jobject, jlong handle, jobject out) {
    void* bytes = out ? env->GetDirectBufferAddress(out) : nullptr;
    const jlong capacity = bytes ? env->GetDirectBufferCapacity(out) : 0;
    return bytes && capacity > 0 && geode_viz_still_read(vizOf(handle), bytes, static_cast<size_t>(capacity)) ? JNI_TRUE
                                                                                                             : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizTapAlong(JNIEnv*, jobject, jlong handle, jlong uptimeNs) {
    return geode_viz_tap_along(vizOf(handle), uptimeNs) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_shakeCreate(JNIEnv*, jobject) {
    return reinterpret_cast<jlong>(geode_shake_create());
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_shakeDestroy(JNIEnv*, jobject, jlong handle) {
    geode_shake_destroy(reinterpret_cast<geode_shake*>(handle));
}

JNIEXPORT jboolean JNICALL Java_dev_geode_engine_bridge_GeodeNative_shakeSample(JNIEnv*, jobject, jlong handle, jlong timeNs,
                                                                              jfloat x, jfloat y, jfloat z) {
    return geode_shake_sample(reinterpret_cast<geode_shake*>(handle), timeNs, x, y, z) ? JNI_TRUE : JNI_FALSE;
}

}
