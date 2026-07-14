#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "can_protocol.h"
#include "generated/can_messages.h"

namespace can {

enum class Bus : uint8_t { High = 0, Low = 1, Powertrain = 2 };

struct CodecError {
    gen::CodecStatus status = gen::CodecStatus::Ok;
    Bus bus = Bus::High;
    uint32_t id = 0;
    bool extended = false;
    uint8_t received_dlc = 0;
    uint8_t expected_dlc = 0;
    uint16_t field_index = UINT16_MAX;
};

template <typename Message>
gen::CodecStatus encode_frame(const Message& message, Frame& frame) noexcept {
    static_assert(Message::kDlc <= sizeof(frame.data),
                  "Classical can::Frame cannot carry this generated message");
    uint8_t payload[sizeof(frame.data)]{};
    const auto status = message.pack(payload, sizeof(payload));
    if (status != gen::CodecStatus::Ok) return status;
    frame = {};
    frame.id = Message::kId;
    frame.extended = Message::kExtended;
    frame.dlc = static_cast<uint8_t>(Message::kDlc);
    for (size_t i = 0; i < Message::kDlc; ++i) frame.data[i] = payload[i];
    return gen::CodecStatus::Ok;
}

template <typename Message>
gen::CodecStatus decode_frame(const Frame& frame, Message& message) noexcept {
    if (frame.id != Message::kId) return gen::CodecStatus::WrongMessageId;
    if (frame.extended != Message::kExtended) return gen::CodecStatus::WrongFrameFormat;
    if (frame.dlc != Message::kDlc) return gen::CodecStatus::UnexpectedLength;
    return Message::unpack(frame.data, frame.dlc, message);
}

class CodecErrorMonitor {
public:
    static constexpr size_t kCapacity = 32;

    struct Entry {
        Bus bus = Bus::High;
        uint32_t id = 0;
        gen::CodecStatus status = gen::CodecStatus::Ok;
        uint16_t field_index = UINT16_MAX;
        uint32_t consecutive = 0;
        uint32_t total = 0;
        uint64_t first_timestamp_us = 0;
        uint64_t last_timestamp_us = 0;
        bool recovery_pending = false;
    };

    void record(const CodecError& error, uint64_t timestamp_us) noexcept {
        Entry* entry = find_or_allocate(error);
        if (!entry) { ++dropped_entries_; return; }
        if (entry->total == 0) entry->first_timestamp_us = timestamp_us;
        entry->last_timestamp_us = timestamp_us;
        ++entry->consecutive;
        ++entry->total;
        entry->recovery_pending = true;
    }

    bool record_valid(Bus bus, uint32_t id, uint64_t timestamp_us) noexcept {
        for (auto& entry : entries_) {
            if (entry.total && entry.bus == bus && entry.id == id && entry.recovery_pending) {
                entry.last_timestamp_us = timestamp_us;
                entry.consecutive = 0;
                entry.recovery_pending = false;
                return true;
            }
        }
        return false;
    }

    static bool should_log(uint32_t consecutive, uint64_t now_us,
                           uint64_t last_log_us) noexcept {
        return consecutive == 1 || now_us - last_log_us >= 5'000'000u;
    }

    const std::array<Entry, kCapacity>& entries() const noexcept { return entries_; }
    uint32_t dropped_entries() const noexcept { return dropped_entries_; }

private:
    Entry* find_or_allocate(const CodecError& error) noexcept {
        Entry* free_entry = nullptr;
        for (auto& entry : entries_) {
            if (!entry.total && !free_entry) free_entry = &entry;
            if (entry.total && entry.bus == error.bus && entry.id == error.id &&
                entry.status == error.status && entry.field_index == error.field_index)
                return &entry;
        }
        if (free_entry) {
            free_entry->bus = error.bus;
            free_entry->id = error.id;
            free_entry->status = error.status;
            free_entry->field_index = error.field_index;
        }
        return free_entry;
    }

    std::array<Entry, kCapacity> entries_{};
    uint32_t dropped_entries_ = 0;
};

} // namespace can
