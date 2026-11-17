#ifndef GEODE_CAPTURE_JAVA_API_H
#define GEODE_CAPTURE_JAVA_API_H

#include <jni.h>

#include "api/geode_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The captures Android has only as Java objects (audio/ingest/CapturePump over audio/ingest/android/JavaCapture):
   a native thread attached to the VM reads the object and writes `ring` itself, so no sample reaches Kotlin.
   The ring must outlive the capture: destroy it only after geode_java_capture_stop. Start, stop, active and quiet
   on one thread that is attached to the VM (the main thread). NULL when the capture could not start; every call
   takes NULL. */
typedef struct geode_java_capture geode_java_capture;
/* Another app's audio: `audio_record` is an initialized AudioRecord (a MediaProjection's playback capture) of
   that rate, channel count and encoding (float or 16-bit), owned from here on: started here, read in blocks of
   geode_capture_read_frames frames, stopped and released when the capture ends. NULL (the record released)
   when it would not start recording. */
GEODE_API geode_java_capture* geode_java_capture_record(JNIEnv* env, jobject audio_record, geode_pcm_ring* ring,
                                                        int rate_hz, int channels, int as_float);
/* The output mix: `visualizer` is an enabled android.media.audiofx.Visualizer of capture size capture_size
   (cut to the ring's runway), sampling_rate_mhz its getSamplingRate (<= 0: 48 kHz), polled with getWaveForm at
   capture_rate_mhz (its getMaxCaptureRate; <= 0: 20 Hz), its unsigned 8-bit mono written as it comes. Borrowed:
   release it after stop. */
GEODE_API geode_java_capture* geode_java_capture_visualizer(JNIEnv* env, jobject visualizer, geode_pcm_ring* ring,
                                                            int capture_size, int sampling_rate_mhz,
                                                            int capture_rate_mhz);
/* Stops and frees the capture: no write follows. */
GEODE_API void geode_java_capture_stop(geode_java_capture*);
/* 0 after stop, or once the object ended the capture (a read error). */
GEODE_API int geode_java_capture_active(const geode_java_capture*);
/* 1 while active and only digital zero was written for 4 s (sound/Silence): a capture the playing app refuses. */
GEODE_API int geode_java_capture_quiet(const geode_java_capture*);

#ifdef __cplusplus
}
#endif

#endif /* GEODE_CAPTURE_JAVA_API_H */
