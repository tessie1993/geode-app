#include <sys/random.h>

#include <cerrno>
#include <cstring>
#include <mutex>
#include <string>

#include "api/geode_api.h"
#include "viz/frame/LinkPair.hpp"
#include "viz/frame/LinkShare.hpp"

namespace pairing = geode::viz::frame::link::pair;
namespace share = geode::viz::frame::link;

static_assert(pairing::kBodyMax == GEODE_LINK_PAIR_BODY_MAX && pairing::kCodeLifeMs == GEODE_LINK_PAIR_CODE_LIFE_MS);

namespace {

bool osRandom(uint8_t* out, size_t n) {
    size_t got = 0;
    while (got < n) {
        const ssize_t r = getrandom(out + got, n - got, 0);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return false;
        got += static_cast<size_t>(r);
    }
    return true;
}

std::mutex gLock;
pairing::Pairing gPairing(osRandom);

}  // namespace

void geode_link_pair_open(int open, int64_t now_ms) {
    std::lock_guard<std::mutex> hold(gLock);
    if (open) {
        gPairing.open(now_ms);
    } else {
        gPairing.close();
    }
}

int geode_link_pair_code(int64_t now_ms, char* out, int cap) {
    std::string shown;
    {
        std::lock_guard<std::mutex> hold(gLock);
        shown = pairing::display(gPairing.code(now_ms));
    }
    const int len = static_cast<int>(shown.size());
    if (out && cap > 0) {
        if (len + 1 > cap) {
            out[0] = '\0';
        } else {
            std::memcpy(out, shown.data(), shown.size());
            out[len] = '\0';
        }
    }
    return len;
}

int geode_link_pair_try(const char* typed, int64_t now_ms, const char* token, char* out, int cap) {
    if (out && cap > 0) out[0] = '\0';
    const std::string path = share::pairedPath(token ? token : "");
    if (path.empty() || !out || cap < static_cast<int>(path.size()) + 1) return pairing::kClosed;
    int status = pairing::kClosed;
    {
        std::lock_guard<std::mutex> hold(gLock);
        status = gPairing.attempt(typed ? typed : "", now_ms);
    }
    if (status == pairing::kPaired) {
        std::memcpy(out, path.data(), path.size());
        out[path.size()] = '\0';
    }
    return status;
}
