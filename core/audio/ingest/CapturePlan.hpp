#pragma once

// The formats a capture asks AudioRecord for, in the order it tries them, and the buffer it asks for (the mic
// and other apps' audio; the shell only makes the AudioRecord calls). The device's own rate goes first: any
// other rate is resampled by the audio HAL, which costs latency and, on some devices, the UNPROCESSED path.
// Then the common rates, most common first, 22.05 kHz for very old hardware. Stereo before mono when the
// source may have two channels; float before 16-bit at each, since the ring takes float as it comes.
//
// Pure, any thread.
namespace geode::audio::ingest {

/** Frames a capture reads at once; the buffer holds at least one such read. */
inline constexpr int kCaptureReadFrames = 1024;
/** The AudioRecord buffer is this many of the platform's minimum, so a late read does not drop audio. */
inline constexpr int kCaptureBufferMinimums = 4;
inline constexpr int kCaptureFallbackRates[] = {48000, 44100, 22050};
/** Every candidate: the device rate and three fallbacks, two channel counts, two encodings. */
inline constexpr int kMaxCaptureFormats = 16;

struct CaptureFormat {
    int rateHz = 0;
    int channels = 1;
    bool asFloat = true;
};

/**
 * Writes the candidates, first to try first, into out (up to capacity) and returns how many there are.
 * deviceRateHz <= 0 is unknown (left out); a device rate equal to a fallback is tried once, first.
 */
int capturePlan(int deviceRateHz, bool stereo, CaptureFormat* out, int capacity);

/**
 * The AudioRecord buffer in bytes for a format whose platform minimum is minBufferBytes:
 * kCaptureBufferMinimums of it, at least kCaptureReadFrames frames of 4-byte samples. 0 when minBufferBytes
 * is not positive (the platform refuses the format: skip it) or channels is not positive.
 */
int captureBufferBytes(int minBufferBytes, int channels);

/**
 * The bytes one read takes (kCaptureReadFrames frames) of `encoding`, numbered as Android's
 * AudioFormat.ENCODING_PCM_* (2 = 16-bit, 3 = 8-bit, 4 = float), at `channels`: the size of the one direct
 * buffer the capture reads into and hands across. 0 for an encoding the ring does not read or no channels.
 */
int captureReadBytes(int encoding, int channels);

}  // namespace geode::audio::ingest
