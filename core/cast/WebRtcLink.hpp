#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "viz/frame/WebRtcRule.hpp"

namespace geode::cast {

// "Show on a computer" over WebRTC: the browser's low-delay path. The one native file that includes
// libdatachannel (third_party/libdatachannel, MPL-2.0, with Mbed TLS for DTLS); tools/network-gate.sh holds
// every other native file to no socket and no WebRTC header. Every decision it follows is in
// viz/frame/WebRtcRule.hpp (host-tested); this only drives the stack. Android build only (CMakeLists.txt).
//
// One per cast stream (CastStream opens it with the address the server is bound to). Each browser's offer
// (LiveHttpServer, POST /<token>/rtc) makes one peer: a send-only H.264 track and, when the browser offered
// Opus, a send-only Opus track, answered with the phone's one host candidate on its Wi-Fi or Ethernet address;
// no ICE server is configured, so nothing is asked of the internet. The cast encoder's frames are copied to
// every connected peer as they leave the codec (no second render, no second encode), each peer starting at a
// keyframe; an Opus encoder the shell runs while peers() > 0 feeds the sound.
//
// Threads: answer() on an HTTP pool thread, video() on the encoder's drain thread, audio() on the Opus
// encoder's; libdatachannel calls back on its own. One mutex guards the peers and the codec config.
class WebRtcLink {
public:
    WebRtcLink(std::string boundDotted, uint32_t bound, int prefix);
    ~WebRtcLink();
    WebRtcLink(const WebRtcLink&) = delete;
    WebRtcLink& operator=(const WebRtcLink&) = delete;

    // The answer to a browser's offer into out; >= 0 on success, else a negative GEODE_WEBRTC_REFUSED_*.
    int answer(std::string_view offer, std::string& out);
    void videoConfig(const uint8_t* data, size_t size);
    void video(const uint8_t* data, size_t size, int64_t ptsUs, bool key);
    void audio(const uint8_t* data, size_t size, int64_t ptsUs);
    int peers() const;
    void close();

    struct Peer;

private:
    void prune();   // lock_ held: drops peers that failed or closed

    const std::string boundDotted_;
    const uint32_t bound_;
    const int prefix_;
    mutable std::mutex lock_;
    std::vector<std::shared_ptr<Peer>> peers_;   // lock_
    std::vector<uint8_t> config_;                // lock_: the encoder's SPS and PPS
    viz::frame::webrtc::H264 profile_;           // lock_: read from config_
    bool closed_ = false;                        // lock_
};

}  // namespace geode::cast
