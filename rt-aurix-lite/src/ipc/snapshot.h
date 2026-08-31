#pragma once
// Snapshot<T> — latest-value semantics for cross-domain publication.
// The producer overwrites; the consumer reads the newest value.
// Host: std::atomic (relaxed). Target: LMU/DLMU + DSYNC later, without
// changing the application code.

#include <atomic>
#include <cstdint>
#include <type_traits>

namespace rta {

// A versioned snapshot: the consumer can detect whether the value changed
// since it last read. Usable with trivially copyable types only.
template <typename T>
class Snapshot {
    static_assert(std::is_trivially_copyable<T>::value,
                  "Snapshot<T> requires a trivially copyable T");
    static_assert(sizeof(T) <= sizeof(std::uint64_t),
                  "Snapshot<T> requires sizeof(T) <= 8 for a lock-free word");

public:
    Snapshot() = default;

    // Publish a new value. Relaxed store is fine for host; target adds
    // the appropriate publication barrier.
    void publish(const T& value) noexcept {
        m_value.store(to_word(value), std::memory_order_relaxed);
    }

    // Read the current value.
    T read() const noexcept {
        return from_word(m_value.load(std::memory_order_relaxed));
    }

private:
    static std::uint64_t to_word(const T& v) noexcept {
        std::uint64_t w = 0;
        // memcpy to avoid aliasing issues; T is <= 8 bytes.
        const unsigned char* p = reinterpret_cast<const unsigned char*>(&v);
        for (unsigned i = 0; i < sizeof(T); ++i) {
            w |= (static_cast<std::uint64_t>(p[i]) << (8 * i));
        }
        return w;
    }

    static T from_word(std::uint64_t w) noexcept {
        T v{};
        unsigned char* p = reinterpret_cast<unsigned char*>(&v);
        for (unsigned i = 0; i < sizeof(T); ++i) {
            p[i] = static_cast<unsigned char>((w >> (8 * i)) & 0xFFu);
        }
        return v;
    }

    std::atomic<std::uint64_t> m_value{0};
};

}  // namespace rta
