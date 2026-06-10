#pragma once
// Result<T, E> — lightweight error-or-value type for embedded use.
// No exceptions, no heap allocation.

#include <cstdint>
#include <type_traits>

namespace os {

template<typename T, typename E = int>
class Result {
    static_assert(!std::is_reference_v<T>, "T must not be a reference");
    static_assert(!std::is_reference_v<E>, "E must not be a reference");

    union { T m_value; E m_error; };
    bool m_ok;

public:
    // Success
    static Result ok(T value) noexcept {
        Result r;
        r.m_ok = true;
        new (&r.m_value) T(std::move(value));
        return r;
    }

    // Error
    static Result err(E error) noexcept {
        Result r;
        r.m_ok = false;
        new (&r.m_error) E(error);
        return r;
    }

    Result(const Result&) = delete;
    Result& operator=(const Result&) = delete;

    Result(Result&& other) noexcept : m_ok(other.m_ok) {
        if (m_ok) new (&m_value) T(std::move(other.m_value));
        else      new (&m_error) E(other.m_error);
    }

    ~Result() {
        if (m_ok) m_value.~T();
        else      m_error.~E();
    }

    explicit operator bool() const noexcept { return m_ok; }
    bool is_ok()  const noexcept { return m_ok; }
    bool is_err() const noexcept { return !m_ok; }

    T& value() noexcept { return m_value; }
    const T& value() const noexcept { return m_value; }

    E& error() noexcept { return m_error; }
    const E& error() const noexcept { return m_error; }

    T&& unwrap() noexcept { return std::move(m_value); }

private:
    Result() = default;  // Constructed via static factory
};

// Void specialisation
template<typename E>
class Result<void, E> {
    E m_error;
    bool m_ok;

public:
    static Result ok() noexcept { Result r; r.m_ok = true; return r; }
    static Result err(E error) noexcept { Result r; r.m_ok = false; r.m_error = error; return r; }

    explicit operator bool() const noexcept { return m_ok; }
    bool is_ok()  const noexcept { return m_ok; }
    bool is_err() const noexcept { return !m_ok; }
    E&   error()  noexcept { return m_error; }

private:
    Result() = default;
};

} // namespace os
