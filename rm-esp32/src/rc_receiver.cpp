#include "rc_receiver.h"
#include "esp_log.h"

static const char* TAG = "rc_rx";

namespace rm {

bool RcReceiver::init() {
    for (uint8_t i = 0; i < kNumRcChannels; ++i) {
        rmt_channel_t ch = channel_from_index(i);
        gpio_num_t pin = gpio_from_index(i);

        rmt_config_t config = RMT_DEFAULT_CONFIG_RX(pin, ch);
        // APB clock is 80 MHz -> clk_div = 80 produces exactly 1 MHz counter (1 tick = 1.0 us)
        config.clk_div = 80;
        config.mem_block_num = 1;
        config.rx_config.filter_en = true;
        config.rx_config.filter_ticks_thresh = 100; // 100 us glitch filter
        config.rx_config.idle_threshold = 8000;     // 8 ms idle threshold (frame period is 20 ms)

        esp_err_t err = rmt_config(&config);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "RMT config failed for CH%u (pin %d): %s", i, pin, esp_err_to_name(err));
            return false;
        }

        // Install driver with 2048-byte ring buffer
        err = rmt_driver_install(ch, 2048, 0);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "RMT driver install failed for CH%u: %s", i, esp_err_to_name(err));
            return false;
        }

        err = rmt_get_ringbuf_handle(ch, &ringbufs_[i]);
        if (err != ESP_OK || ringbufs_[i] == nullptr) {
            ESP_LOGE(TAG, "RMT get ringbuf failed for CH%u", i);
            return false;
        }

        err = rmt_rx_start(ch, true);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "RMT rx_start failed for CH%u", i);
            return false;
        }

        past_filtered_us_[i] = (i == 0 || i == 5) ? kPulseCenterUs : kPulseMinValidUs;
    }

    ESP_LOGI(TAG, "Initialized 6 RMT pulse channels at 1.0 us/tick (idle_threshold=8000us)");
    return true;
}

void RcReceiver::sample(uint32_t now_ms) {
    // 1. Drain ringbuffers and capture newest valid high pulse width
    for (uint8_t i = 0; i < kNumRcChannels; ++i) {
        if (!ringbufs_[i]) continue;

        size_t rx_size = 0;
        rmt_item32_t* item = nullptr;

        while ((item = static_cast<rmt_item32_t*>(xRingbufferReceive(ringbufs_[i], &rx_size, 0))) != nullptr) {
            if (rx_size >= sizeof(rmt_item32_t)) {
                size_t num_items = rx_size / sizeof(rmt_item32_t);
                for (size_t k = 0; k < num_items; ++k) {
                    uint32_t dur0 = item[k].duration0;
                    uint32_t lvl0 = item[k].level0;
                    uint32_t dur1 = item[k].duration1;
                    uint32_t lvl1 = item[k].level1;

                    uint32_t duration_us = (lvl0 == 1) ? dur0 : ((lvl1 == 1) ? dur1 : 0);
                    if (duration_us >= kPulseMinValidUs && duration_us <= kPulseMaxValidUs) {
                        raw_high_us_[i] = duration_us;
                        last_edge_time_ms_[i] = now_ms;
                    }
                }
            }
            vRingbufferReturnItem(ringbufs_[i], item);
        }
    }

    // 2. Apply Deadband & Hysteresis to raw values
    for (uint8_t i = 0; i < kNumRcChannels; ++i) {
        if (now_ms - last_edge_time_ms_[i] <= kSignalLossTimeoutMs) {
            int32_t diff = static_cast<int32_t>(raw_high_us_[i]) - static_cast<int32_t>(past_filtered_us_[i]);
            // Apply 4 us jitter filter
            if (std::abs(diff) >= 4) {
                past_filtered_us_[i] = raw_high_us_[i];
            }
        }
    }

    // 3. Decode signals using pure decoder
    RcSnapshot snap = decode_rc_signals(past_filtered_us_, last_edge_time_ms_, now_ms);

    // 4. Store atomically
    snap_steering_.store(snap.steering_deg, std::memory_order_relaxed);
    snap_brake_.store(snap.brake_stroke_mm, std::memory_order_relaxed);
    snap_speed_trim_.store(snap.speed_trim, std::memory_order_relaxed);
    snap_aux_pass_.store(snap.aux_pass, std::memory_order_relaxed);
    snap_ignition_.store(snap.ignition, std::memory_order_relaxed);
    snap_gear_.store(snap.gear, std::memory_order_relaxed);
    snap_valid_.store(snap.signal_valid, std::memory_order_relaxed);
    snap_last_update_ms_.store(snap.last_update_ms, std::memory_order_relaxed);
}

RcSnapshot RcReceiver::snapshot() const {
    RcSnapshot snap;
    snap.steering_deg = snap_steering_.load(std::memory_order_relaxed);
    snap.brake_stroke_mm = snap_brake_.load(std::memory_order_relaxed);
    snap.speed_trim = snap_speed_trim_.load(std::memory_order_relaxed);
    snap.aux_pass = snap_aux_pass_.load(std::memory_order_relaxed);
    snap.ignition = snap_ignition_.load(std::memory_order_relaxed);
    snap.gear = snap_gear_.load(std::memory_order_relaxed);
    snap.signal_valid = snap_valid_.load(std::memory_order_relaxed);
    snap.last_update_ms = snap_last_update_ms_.load(std::memory_order_relaxed);
    return snap;
}

}  // namespace rm
