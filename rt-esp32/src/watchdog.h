#pragma once
#include <cstdint>
#include "config.h"
namespace rt {
class CmdWatchdog {
public:
    void init() { m_last_feed=-kCmdStaleTimeoutMs*1000; }
    void feed(int64_t now_us) { m_last_feed=now_us; }
    bool is_stale(int64_t now_us) const { return (now_us-m_last_feed)>int64_t(kCmdStaleTimeoutMs)*1000; }
private:
    int64_t m_last_feed=0;
};
}
