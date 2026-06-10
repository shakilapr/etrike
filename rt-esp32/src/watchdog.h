#pragma once
// Command staleness watchdog.
// If Jetson stops sending HOST_DRIVE_CMD (0x300), triggers controlled stop.

#include <cstdint>

namespace rt {

class Watchdog {
public:
    Watchdog() = default;

    void init();                // Record initial timestamp
    void feed();                // Call on every valid 0x300 receipt
    bool is_stale() const;      // True if last command is older than timeout
    bool is_tripped() const { return m_tripped; }

private:
    int64_t m_last_feed_us = 0;
    bool    m_tripped      = false;
};

}  // namespace rt
