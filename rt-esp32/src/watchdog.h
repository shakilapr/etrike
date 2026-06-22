#pragma once
#include <cstdint>
#include <atomic>
#include "config.h"
namespace rt {
class CmdWatchdog {
public:
    void init() { m_last_feed.store(-kCmdStaleTimeoutMs*1000); }
    void feed(int64_t now_us) { m_last_feed.store(now_us); }
    bool is_stale(int64_t now_us) { return (now_us-m_last_feed.load())>int64_t(kCmdStaleTimeoutMs)*1000; }
private:
    std::atomic<int64_t> m_last_feed{0};
};
}
