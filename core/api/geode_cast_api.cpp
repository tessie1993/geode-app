// The cast's HTTP server (core/cast/LiveServer) and its encoders (core/cast/CastEncoder): the C ABI. Android
// build only (sockets, MediaCodec).
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "api/geode_api.h"
#include "api/geode_ingest_handles.hpp"
#include "cast/CastEncoder.hpp"
#include "cast/LiveServer.hpp"
#include "cast/WebRtcLink.hpp"
#include "output/CastAudioPump.hpp"
#include "output/android/MediaCodecSink.hpp"
#include "viz/frame/FixedRateClock.hpp"

using geode::cast::CastEncoder;
using geode::cast::LiveServer;
using geode::cast::WebRtcLink;

namespace {

LiveServer* from(int64_t handle) { return reinterpret_cast<LiveServer*>(static_cast<intptr_t>(handle)); }

std::string text(const LiveServer& server, int which) {
    switch (which) {
        case GEODE_CAST_TEXT_PLAYLIST_URL: return server.playlistUrl();
        case GEODE_CAST_TEXT_SHARE_URL: return server.shareUrl();
        case GEODE_CAST_TEXT_LABEL: return server.label();
        case GEODE_CAST_TEXT_REFUSALS: return server.refusals();
        case GEODE_CAST_TEXT_PAIR_URL: return server.pairUrl();
        default: return {};
    }
}

}  // namespace

int64_t geode_cast_server_open(const char* bound_dotted, uint32_t bound, int prefix, int64_t webrtc) {
    if (!bound_dotted || bound == 0) return 0;
    try {
        WebRtcLink* link = reinterpret_cast<WebRtcLink*>(static_cast<intptr_t>(webrtc));
        auto answer = [link](std::string_view offer, std::string& out) -> int {
            try {
                return link ? link->answer(offer, out) : GEODE_WEBRTC_REFUSED_FAILED;
            } catch (...) {
                return GEODE_WEBRTC_REFUSED_FAILED;
            }
        };
        return static_cast<int64_t>(reinterpret_cast<intptr_t>(new LiveServer(bound_dotted, bound, prefix, answer)));
    } catch (...) {
        return 0;
    }
}

int geode_cast_server_start(int64_t handle) {
    LiveServer* server = from(handle);
    if (!server) return 0;
    try {
        return server->start();
    } catch (...) {
        return 0;
    }
}

void geode_cast_server_stop(int64_t handle) {
    LiveServer* server = from(handle);
    if (!server) return;
    try {
        server->stop();
    } catch (...) {
    }
}

void geode_cast_server_close(int64_t handle) {
    LiveServer* server = from(handle);
    if (!server) return;
    try {
        server->stop();
    } catch (...) {
    }
    delete server;
}

void geode_cast_server_admit(int64_t handle, uint32_t tv, int wants) {
    LiveServer* server = from(handle);
    if (!server) return;
    try {
        server->admit(tv, wants);
    } catch (...) {
    }
}

void geode_cast_server_write(int64_t handle, const uint8_t* data, size_t size) {
    LiveServer* server = from(handle);
    if (!server || !data || size == 0) return;
    try {
        server->segments().write(data, size);
    } catch (...) {
        // Out of memory: the bytes are lost, no segment is cut and the freeze watchdog ends the cast.
    }
}

int geode_cast_server_text(int64_t handle, int which, char* out, int cap) {
    if (!out || cap <= 0) return 0;
    out[0] = '\0';
    const LiveServer* server = from(handle);
    if (!server) return 0;
    try {
        const std::string value = text(*server, which);
        if (value.size() + 1 > static_cast<size_t>(cap)) return 0;
        std::memcpy(out, value.data(), value.size());
        out[value.size()] = '\0';
        return static_cast<int>(value.size());
    } catch (...) {
        out[0] = '\0';
        return 0;
    }
}

void geode_cast_server_state(int64_t handle, int64_t* out) {
    if (!out) return;
    for (int i = 0; i < GEODE_CAST_STATE; ++i) out[i] = 0;
    const LiveServer* server = from(handle);
    if (!server) return;
    try {
        const geode::cast::LiveSegments& stream = server->segments();
        out[GEODE_CAST_STATE_LAST_FETCH_MS] = server->lastReceiverFetchMs();
        out[GEODE_CAST_STATE_SEGMENT_AGE_MS] = stream.msSinceLastSegment();
        out[GEODE_CAST_STATE_READY] = stream.playlist().empty() ? 0 : 1;
        out[GEODE_CAST_STATE_AS_MUXED] = stream.servedAsMuxed();
        out[GEODE_CAST_STATE_BROKEN] = stream.broken() ? 1 : 0;
    } catch (...) {
    }
}

struct geode_cast_encoder {
    const geode_pcm_ring* music;  // compared with each pump's ring only; the pump holds the ring itself
    geode::output::CastAudioPump pump;
    std::unique_ptr<CastEncoder> encoder;
};

geode_cast_encoder* geode_cast_encoder_open(int64_t server, int64_t webrtc, const geode_pcm_ring* music) {
    using Cast = geode::viz::frame::CastFormat;
    constexpr int kRateHz = geode::viz::frame::CastAudio::kDefaultRateHz;
    constexpr int kChannels = 2;
    try {
        LiveServer* stream = from(server);
        WebRtcLink* link = reinterpret_cast<WebRtcLink*>(static_cast<intptr_t>(webrtc));
        CastEncoder::Format format;
        format.width = Cast::kWidth;
        format.height = Cast::kHeight;
        format.fps = Cast::kFps;
        format.keyframeSec = Cast::kKeyframeIntervalSec;
        format.videoBitRate = Cast::kVideoBitrate;
        format.audioBitRate = Cast::kAudioBitrate;
        format.rateHz = kRateHz;
        format.channels = kChannels;
        // Called on the encoder's thread: nothing may leave them (an exception there would end the process).
        CastEncoder::Outputs out;
        if (stream) {
            out.stream = [stream](const uint8_t* d, size_t n) {
                try {
                    stream->segments().write(d, n);
                } catch (...) {
                    // Out of memory: no segment is cut and the freeze watchdog ends the cast.
                }
            };
        }
        if (link) {
            out.videoConfig = [link](const uint8_t* d, size_t n) {
                try {
                    link->videoConfig(d, n);
                } catch (...) {
                }
            };
            out.video = [link](const uint8_t* d, size_t n, int64_t ptsUs, bool key) {
                try {
                    link->video(d, n, ptsUs, key);
                } catch (...) {
                }
            };
            out.opus = [link](const uint8_t* d, size_t n, int64_t ptsUs) {
                try {
                    link->audio(d, n, ptsUs);
                } catch (...) {
                }
            };
            out.peers = [link]() -> int {
                try {
                    return link->peers();
                } catch (...) {
                    return 0;
                }
            };
        }
        std::unique_ptr<CastEncoder> encoder =
            CastEncoder::open(geode::output::sink::mediaCodecs(-1), format, std::move(out));
        if (!encoder) return nullptr;
        std::shared_ptr<const geode::audio::ingest::PcmRing> ring = music ? music->ring : nullptr;
        // Held until start() returns: a thread that cannot be created throws, and the encoder must not leak started.
        std::unique_ptr<geode_cast_encoder> made(new geode_cast_encoder{
            music, geode::output::CastAudioPump(std::move(ring), kRateHz, kChannels), std::move(encoder)});
        made->encoder->start();
        return made.release();
    } catch (...) {
        return nullptr;
    }
}

void* geode_cast_encoder_window(const geode_cast_encoder* e) { return e ? e->encoder->inputWindow() : nullptr; }

void geode_cast_encoder_pump(geode_cast_encoder* e, const geode_pcm_ring* current, int64_t until_us) {
    if (!e) return;
    try {
        const int frames = e->pump.pump(current != nullptr && current == e->music, until_us);
        if (frames > 0) e->encoder->queuePcm(e->pump.block(), frames);
    } catch (...) {
    }
}

int geode_cast_encoder_failed(const geode_cast_encoder* e) { return e && e->encoder->failed() ? 1 : 0; }

void geode_cast_encoder_close(geode_cast_encoder* e) { std::unique_ptr<geode_cast_encoder> owned(e); }
