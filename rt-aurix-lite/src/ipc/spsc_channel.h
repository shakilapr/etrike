#pragma once
// Lock-free single-producer / single-consumer bounded channel.
// Used for cross-domain / cross-core message passing. Host: std::atomic
// indexes. Target: same interface, LMU/DLMU placement + DSYNC later.
//
// Semantics: push returns false when full (caller drops/counts); pop
// returns false when empty. Single producer and single consumer only.

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace rta {

template <typename T, std::size_t N>
class SpscChannel {
    static_assert(N > 0, "SpscChannel requires N > 0");

public:
    SpscChannel() = default;

    // Producer: enqueue. Returns false if full (caller policy: drop/count).
    bool push(const T& value) noexcept {
        const std::size_t head = m_head.load(std::memory_order_relaxed);
        const std::size_t next = (head + 1) % (N + 1);  // +1 to distinguish full
        if (next == m_tail.load(std::memory_order_acquire)) return false;  // full
        m_buffer[head] = value;
        m_head.store(next, std::memory_order_release);
        return true;
    }

    // Consumer: dequeue. Returns false if empty.
    bool pop(T& out) noexcept {
        const std::size_t tail = m_tail.load(std::memory_order_relaxed);
        if (tail == m_head.load(std::memory_order_acquire)) return false;  // empty
        out = m_buffer[tail];
        m_tail.store((tail + 1) % (N + 1), std::memory_order_release);
        return true;
    }

    bool empty() const noexcept {
        return m_head.load(std::memory_order_relaxed)
            == m_tail.load(std::memory_order_relaxed);
    }

    bool full() const noexcept {
        const std::size_t head = m_head.load(std::memory_order_relaxed);
        const std::size_t next = (head + 1) % (N + 1);
        return next == m_tail.load(std::memory_order_relaxed);
    }

    std::size_t capacity() const noexcept { return N; }

private:
    std::array<T, N + 1> m_buffer{};  // one slot headroom to disambiguate full
    std::atomic<std::size_t> m_head{0};
    std::atomic<std::size_t> m_tail{0};
};

}  // namespace rta
