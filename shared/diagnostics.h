// DiagnosticManager — Phase B runtime reporting (no reaction, no detection).
//
// Embedded-safe: fixed-size in-memory bookkeeping only. No std:: containers,
// no malloc, no locks, no synchronous CAN, no flash I/O, no expensive snapshots.
// Indexed by the dense IMPLEMENTED-only DiagId mapping emitted by the generator
// (etrike::diagnostics::diag_index), giving O(1) access without STL.
//
// Generated registry header: protocol/generated/cpp/diagnostics.hpp
#pragma once

#include <cstddef>
#include <cstdint>

#include "protocol/generated/cpp/diagnostics.hpp"

namespace etrike::diagnostics {

// Lifecycle state of a single diagnostic record.
enum class DiagState : std::uint8_t {
    Pending = 0,    // reserved — not emitted in Phase B
    Active = 1,     // fault condition currently asserted
    Latched = 2,    // fault physically cleared, retained pending operator ack
    Recovered = 3,  // transient fault returned to healthy state
    Cleared = 4,    // operator ack / reset complete
};

// On-wire report shape for 0x601 / 0x621 / 0x631 (DiagEventRpt).
struct DiagReport {
    DiagId id;
    DiagState state;
    std::uint8_t occurrence_count;  // saturating 0..255
    std::uint8_t report_counter;    // manager serialization counter (mod 256)
    std::uint8_t flags;
    std::uint16_t snapshot_data;
};

inline constexpr std::uint8_t kDiagFlagFirstLocalEstopCause = 0x01;

struct DiagRuntime {
    DiagState state = DiagState::Cleared;
    std::uint8_t occurrence_count = 0;
    std::uint16_t snapshot = 0;
    bool pending_report = false;
    bool reset_count_after_drain = false;
};

class DiagnosticManager {
public:
    static constexpr std::size_t kCapacity = kImplementedDiagCount;

    // Bookkeeping only — never transmits, never allocates, never reacts.
    void raise(DiagId id, std::uint16_t snapshot = 0) noexcept {
        const int idx = diag_index(id);
        if (idx < 0) return;
        const DiagMetaLite& meta = kImplementedDiagMeta[idx];
        DiagRuntime& rec = records_[static_cast<std::size_t>(idx)];
        if (rec.state == DiagState::Active) {
            // Re-assertion: update snapshot if changed; never re-increment count
            // or burst CAN spam.
            if (rec.snapshot != snapshot) rec.snapshot = snapshot;
            return;
        }
        rec.state = DiagState::Active;
        if (rec.occurrence_count < 0xFF) ++rec.occurrence_count;
        rec.snapshot = snapshot;
        rec.pending_report = true;
        if (meta.is_estop_cause && !estop_episode_claimed_) {
            estop_episode_claimed_ = true;
            estop_first_report_flag_pending_ = true;
        }
    }

    void recover(DiagId id) noexcept {
        const int idx = diag_index(id);
        if (idx < 0) return;
        DiagRuntime& rec = records_[static_cast<std::size_t>(idx)];
        if (rec.state != DiagState::Active) return;
        rec.state = kImplementedDiagMeta[idx].latching ? DiagState::Latched : DiagState::Recovered;
        rec.pending_report = true;
    }

    void clear(DiagId id) noexcept {
        const int idx = diag_index(id);
        if (idx < 0) return;
        DiagRuntime& rec = records_[static_cast<std::size_t>(idx)];
        if (rec.state == DiagState::Latched || rec.state == DiagState::Recovered) {
            rec.state = DiagState::Cleared;
            rec.pending_report = true;
            rec.reset_count_after_drain = true;
        }
    }

    void clear_all() noexcept {
        for (std::size_t i = 0; i < kCapacity; ++i) {
            DiagRuntime& rec = records_[i];
            if (rec.state == DiagState::Latched || rec.state == DiagState::Recovered) {
                rec.state = DiagState::Cleared;
                rec.pending_report = true;
                rec.reset_count_after_drain = true;
            }
        }
    }

    // Called strictly by the safety subsystem after Phase-A SAFETY_CLEAR completes.
    void on_estop_episode_cleared() noexcept {
        estop_episode_claimed_ = false;
        estop_first_report_flag_pending_ = false;
    }

    // Dequeue the next report for transmission. Increments report_counter.
    // When a CLEARED report is drained, its occurrence_count resets to 0 so the
    // next incident begins at 1.
    bool pop_pending_report(DiagReport& out) noexcept {
        for (std::size_t n = 0; n < kCapacity; ++n) {
            const std::size_t i = (pop_cursor_ + n) % kCapacity;
            if (!records_[i].pending_report) continue;
            records_[i].pending_report = false;
            pop_cursor_ = (i + 1u) % kCapacity;
            const DiagMetaLite& meta = kImplementedDiagMeta[i];
            const DiagRuntime& rec = records_[i];
            out.id = meta.id;
            out.state = rec.state;
            out.occurrence_count = rec.occurrence_count;
            out.report_counter = ++report_counter_;
            out.snapshot_data = rec.snapshot;
            std::uint8_t flags = 0;
            if (meta.is_estop_cause && estop_first_report_flag_pending_) {
                flags |= kDiagFlagFirstLocalEstopCause;
                estop_first_report_flag_pending_ = false;
            }
            out.flags = flags;
            if (rec.state == DiagState::Cleared && rec.reset_count_after_drain) {
                records_[i].occurrence_count = 0;
                records_[i].reset_count_after_drain = false;
            }
            return true;
        }
        return false;
    }

    // Re-mark ACTIVE/LATCHED records for transmission (~1 Hz max) so a dropped
    // frame is recovered.
    void replay_active_set() noexcept {
        for (std::size_t i = 0; i < kCapacity; ++i) {
            if (records_[i].state == DiagState::Active || records_[i].state == DiagState::Latched) {
                records_[i].pending_report = true;
            }
        }
    }

    DiagState state_of(DiagId id) const noexcept {
        const int idx = diag_index(id);
        if (idx < 0) return DiagState::Pending;
        return records_[static_cast<std::size_t>(idx)].state;
    }

    std::uint8_t occurrence_of(DiagId id) const noexcept {
        const int idx = diag_index(id);
        if (idx < 0) return 0;
        return records_[static_cast<std::size_t>(idx)].occurrence_count;
    }

    bool is_pending_report(DiagId id) const noexcept {
        const int idx = diag_index(id);
        if (idx < 0) return false;
        return records_[static_cast<std::size_t>(idx)].pending_report;
    }

private:
    DiagRuntime records_[kCapacity] = {};
    std::uint8_t report_counter_ = 0;
    bool estop_episode_claimed_ = false;
    bool estop_first_report_flag_pending_ = false;
    std::size_t pop_cursor_ = 0;
};

}  // namespace etrike::diagnostics
