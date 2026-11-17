#pragma once

#include <jni.h>

#include <memory>

#include "audio/ingest/CapturePump.hpp"

// The captures Android has only as Java objects, built, read and released here through JNI: the shell hands
// down at most the consent token, nothing in Kotlin builds the object or reads a sample. CapturePump's thread
// reads them attached to the VM. Android only, never in a host test.
namespace geode::audio::ingest {

// What a reader writes: Android's AudioFormat.ENCODING_PCM_* (as GEODE_PCM_*), and the bytes one read takes.
struct JavaCaptureFormat {
    int rateHz = 0;
    int channels = 0;
    int encoding = 0;
    int readBytes = 0;
};

// Another app's audio: an AudioRecord over `projection` (a MediaProjection the user consented to) in the first
// format of capturePlan the platform takes, started here; end stops and releases it. Null when none would
// record. Caller's thread (attached).
std::unique_ptr<CaptureReader> playbackCaptureReader(JNIEnv* env, jobject projection, JavaCaptureFormat* format);

// The output mix: a Visualizer on audio session 0, its largest capture size cut to maxBytes, scaled as played,
// enabled here and polled with getWaveForm at its fastest capture rate; end disables and releases it. Null when
// the platform refuses it (no permission, a device without it, another app holding it). Caller's thread
// (attached).
std::unique_ptr<CaptureReader> outputMixReader(JNIEnv* env, int maxBytes, JavaCaptureFormat* format);

}  // namespace geode::audio::ingest
