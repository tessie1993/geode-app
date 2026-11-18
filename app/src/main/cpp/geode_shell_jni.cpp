// JNI for the shell's rules (core/shell through api/geode_shell_api.h): marshalling only.
#include <jni.h>

#include <string>
#include <vector>

#include "api/geode_shell_api.h"
#include "jni_util.hpp"

namespace {

using geode::jni::Utf8Chars;
using geode::jni::utf8Lines;

// A Java String[] as the standard UTF-8 texts and pointers the C ABI takes. Not ok() when an entry could not be
// read (out of memory, with the exception pending for the caller's Java frame).
class Ids {
public:
    Ids(JNIEnv* env, jobjectArray array) {
        if (env->ExceptionCheck()) {
            ok_ = false;
            return;
        }
        const jsize count = array ? env->GetArrayLength(array) : 0;
        texts_.reserve(static_cast<size_t>(count));
        for (jsize i = 0; i < count; ++i) {
            auto* entry = static_cast<jstring>(env->GetObjectArrayElement(array, i));
            {
                const Utf8Chars chars(env, entry);
                if (entry && !chars.get()) ok_ = false;
                texts_.emplace_back(chars.get() ? chars.get() : "");
            }
            env->DeleteLocalRef(entry);
            if (!ok_ || env->ExceptionCheck()) {
                ok_ = false;
                return;
            }
        }
        for (const std::string& text : texts_) pointers_.push_back(text.c_str());
    }
    Ids(const Ids&) = delete;
    Ids& operator=(const Ids&) = delete;
    bool ok() const { return ok_; }
    const char* const* get() const { return pointers_.data(); }
    int count() const { return static_cast<int>(pointers_.size()); }

private:
    std::vector<std::string> texts_;
    std::vector<const char*> pointers_;
    bool ok_ = true;
};

// A Java int[] copied out, padded with zeros to at least `count`.
std::vector<int> intsIn(JNIEnv* env, jintArray array, int count) {
    const jsize n = array ? env->GetArrayLength(array) : 0;
    std::vector<int> out(static_cast<size_t>(n > count ? n : count), 0);
    if (n > 0) env->GetIntArrayRegion(array, 0, n, reinterpret_cast<jint*>(out.data()));
    return out;
}

// A Java String that could not be read must not reach native as "absent".
bool unread(jstring str, const Utf8Chars& chars) { return str && !chars.get(); }

// A text-out list call's whole list as a String[]; `call(out, capacity)` returns the length it needs.
template <typename Call>
jobjectArray linesOf(JNIEnv* env, Call call) {
    char fixed[1024];
    const int need = call(fixed, static_cast<int>(sizeof fixed));
    if (need < static_cast<int>(sizeof fixed)) return utf8Lines(env, fixed);
    std::vector<char> whole(static_cast<size_t>(need) + 1);
    call(whole.data(), need + 1);
    return utf8Lines(env, whole.data());
}

jintArray intsOf(JNIEnv* env, const std::vector<int>& values) {
    const auto count = static_cast<jsize>(values.size());
    jintArray out = env->NewIntArray(count);
    if (out && count > 0) env->SetIntArrayRegion(out, 0, count, reinterpret_cast<const jint*>(values.data()));
    return out;
}

}  // namespace

extern "C" {

JNIEXPORT jintArray JNICALL Java_dev_geode_engine_bridge_ShellNative_exportFpsOptions(JNIEnv* env, jobject) {
    std::vector<int> options(static_cast<size_t>(geode_export_fps_options(nullptr, 0)));
    geode_export_fps_options(options.data(), static_cast<int>(options.size()));
    return intsOf(env, options);
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_ShellNative_exportFpsRead(JNIEnv*, jobject, jint stored, jint fallback) {
    return geode_export_fps_read(stored, fallback);
}

JNIEXPORT jintArray JNICALL Java_dev_geode_engine_bridge_ShellNative_notificationButtonsOrdered(JNIEnv* env, jobject,
                                                                                            jint chosen) {
    const auto mask = static_cast<unsigned>(chosen);
    std::vector<int> ordered(static_cast<size_t>(geode_notification_buttons_ordered(mask, nullptr, 0)));
    geode_notification_buttons_ordered(mask, ordered.data(), static_cast<int>(ordered.size()));
    return intsOf(env, ordered);
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_ShellNative_notificationButtonsToggled(JNIEnv*, jobject, jint chosen,
                                                                                       jint button, jboolean on) {
    return static_cast<jint>(geode_notification_buttons_toggled(static_cast<unsigned>(chosen), button, on == JNI_TRUE ? 1 : 0));
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_ShellNative_behaviourLastScreen(JNIEnv*, jobject, jint last) {
    return geode_behaviour_last_screen(last);
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_ShellNative_behaviourOpensOn(JNIEnv*, jobject, jint start, jint last) {
    return geode_behaviour_opens_on(start, last);
}

JNIEXPORT jobjectArray JNICALL Java_dev_geode_engine_bridge_ShellNative_styleShelfToggleFavourite(JNIEnv* env, jobject,
                                                                                                jobjectArray favourites,
                                                                                                jstring id) {
    const Ids ids(env, favourites);
    const Utf8Chars chars(env, id);
    if (!ids.ok() || unread(id, chars)) return nullptr;
    return linesOf(env, [&](char* out, int capacity) {
        return geode_style_shelf_toggle_favourite(ids.get(), ids.count(), chars.get(), out, capacity);
    });
}

JNIEXPORT jobjectArray JNICALL Java_dev_geode_engine_bridge_ShellNative_styleShelfPushRecent(JNIEnv* env, jobject,
                                                                                           jobjectArray recents, jstring id) {
    const Ids ids(env, recents);
    const Utf8Chars chars(env, id);
    if (!ids.ok() || unread(id, chars)) return nullptr;
    return linesOf(env, [&](char* out, int capacity) {
        return geode_style_shelf_push_recent(ids.get(), ids.count(), chars.get(), out, capacity);
    });
}

JNIEXPORT jobjectArray JNICALL Java_dev_geode_engine_bridge_ShellNative_styleShelfRemoveRecent(JNIEnv* env, jobject,
                                                                                             jobjectArray recents, jstring id) {
    const Ids ids(env, recents);
    const Utf8Chars chars(env, id);
    if (!ids.ok() || unread(id, chars)) return nullptr;
    return linesOf(env, [&](char* out, int capacity) {
        return geode_style_shelf_remove_recent(ids.get(), ids.count(), chars.get(), out, capacity);
    });
}

JNIEXPORT jobjectArray JNICALL Java_dev_geode_engine_bridge_ShellNative_styleShelfYours(JNIEnv* env, jobject,
                                                                                      jobjectArray favourites,
                                                                                      jobjectArray recents,
                                                                                      jobjectArray order, jstring onScreen) {
    const Ids favouriteIds(env, favourites);
    const Ids recentIds(env, recents);
    const Ids orderIds(env, order);
    const Utf8Chars screen(env, onScreen);
    if (!favouriteIds.ok() || !recentIds.ok() || !orderIds.ok() || unread(onScreen, screen)) return nullptr;
    return linesOf(env, [&](char* out, int capacity) {
        return geode_style_shelf_yours(favouriteIds.get(), favouriteIds.count(), recentIds.get(), recentIds.count(),
                                       orderIds.get(), orderIds.count(), screen.get(), out, capacity);
    });
}

JNIEXPORT jintArray JNICALL Java_dev_geode_engine_bridge_ShellNative_presetFilters(JNIEnv* env, jobject, jobjectArray folders) {
    const Ids ids(env, folders);
    if (!ids.ok()) return nullptr;
    const int chips = geode_preset_filters(ids.get(), ids.count(), nullptr, 0);
    std::vector<int> pairs(static_cast<size_t>(2 * chips));
    geode_preset_filters(ids.get(), ids.count(), pairs.data(), chips);
    return intsOf(env, pairs);
}

JNIEXPORT jintArray JNICALL Java_dev_geode_engine_bridge_ShellNative_presetApply(JNIEnv* env, jobject, jobjectArray names,
                                                                             jobjectArray folders, jintArray flags,
                                                                             jint filterKind, jstring filterFolder) {
    const Ids nameIds(env, names);
    const Ids folderIds(env, folders);
    const Utf8Chars folder(env, filterFolder);
    if (!nameIds.ok() || !folderIds.ok() || unread(filterFolder, folder)) return nullptr;
    const int count = nameIds.count() < folderIds.count() ? nameIds.count() : folderIds.count();
    const std::vector<int> marks = intsIn(env, flags, count);
    std::vector<int> rows(static_cast<size_t>(count));
    const int shown = geode_preset_apply(nameIds.get(), folderIds.get(), marks.data(), count, filterKind, folder.get(), rows.data());
    rows.resize(static_cast<size_t>(shown));
    return intsOf(env, rows);
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_ShellNative_presetApplied(JNIEnv* env, jobject, jobjectArray names,
                                                                          jintArray flags, jstring lastTapped) {
    const Ids nameIds(env, names);
    const Utf8Chars tapped(env, lastTapped);
    if (!nameIds.ok() || unread(lastTapped, tapped)) return -1;
    const std::vector<int> marks = intsIn(env, flags, nameIds.count());
    return geode_preset_applied(nameIds.get(), marks.data(), nameIds.count(), tapped.get());
}

}  // extern "C"
