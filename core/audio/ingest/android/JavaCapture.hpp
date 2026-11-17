#pragma once

#include <jni.h>

#include <memory>

#include "audio/ingest/CapturePump.hpp"

// The captures Android hands over only as Java objects, read by CapturePump's native thread attached to the VM:
// the shell builds the object, nothing in Kotlin reads a sample. Android only, never in a host test.
namespace geode::audio::ingest {

// Another app's audio: `record`, an initialized AudioRecord over a MediaProjection, is owned from here on. It is
// started here and read in blocks of readBytes into a direct buffer; end stops and releases it. Null, with the
// record already released, when it would not start recording. Caller's thread (attached).
std::unique_ptr<CaptureReader> audioRecordReader(JNIEnv* env, jobject record, int readBytes);

// The output mix: `visualizer`, enabled with capture size captureSize, polled with getWaveForm every periodMs,
// the first keepBytes of each capture kept. A Visualizer only delivers by callback or by that poll, and the poll
// keeps the array out of Kotlin. Borrowed: its owner releases it once the pump has stopped. Caller's thread
// (attached).
std::unique_ptr<CaptureReader> visualizerReader(JNIEnv* env, jobject visualizer, int captureSize, int keepBytes,
                                                int periodMs);

}  // namespace geode::audio::ingest
