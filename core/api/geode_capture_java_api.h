#ifndef GEODE_CAPTURE_JAVA_API_H
#define GEODE_CAPTURE_JAVA_API_H

#include <jni.h>

#include "api/geode_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The captures Android has only as Java objects (audio/ingest/CapturePump over audio/ingest/android/JavaCapture):
   built, read on a native thread attached to the VM and released natively, so the shell neither builds the
   object nor touches a sample. The ring must outlive the capture: destroy it only after geode_java_capture_stop.
   Start, stop, active and quiet on one thread that is attached to the VM (the main thread). NULL when the
   capture could not start; every call takes NULL. */
typedef struct geode_java_capture geode_java_capture;
/* Another app's audio: an AudioRecord over `media_projection` (the MediaProjection the user consented to) in the
   first format of the capture plan the platform takes (audio/ingest/CapturePlan), writing `ring`. NULL when
   none would record. */
GEODE_API geode_java_capture* geode_java_capture_playback(JNIEnv* env, jobject media_projection,
                                                          geode_pcm_ring* ring);
/* The output mix: a Visualizer on audio session 0, its capture cut to the ring's runway, polled at its fastest
   capture rate, its unsigned 8-bit mono written as it comes after a new epoch of `ring`. NULL when the platform
   refuses it (no RECORD_AUDIO, a device without it, another app holding it). */
GEODE_API geode_java_capture* geode_java_capture_output_mix(JNIEnv* env, geode_pcm_ring* ring);
/* Stops and frees the capture and releases its Java object: no write follows. */
GEODE_API void geode_java_capture_stop(geode_java_capture*);
/* 0 after stop, or once the object ended the capture (a read error). */
GEODE_API int geode_java_capture_active(const geode_java_capture*);
/* 1 while active and only digital zero was written for 4 s (sound/Silence): a capture the playing app refuses. */
GEODE_API int geode_java_capture_quiet(const geode_java_capture*);

#ifdef __cplusplus
}
#endif

#endif /* GEODE_CAPTURE_JAVA_API_H */
