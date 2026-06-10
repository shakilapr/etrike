#pragma once
// Queue<T> — type-safe FreeRTOS queue wrapper.
// Eliminates void* casting and sizeof mistakes.

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <type_traits>

namespace os {

template<typename T, size_t N = 4>
class Queue {
    static_assert(std::is_trivially_copyable_v<T>, "Queue<T> requires trivially copyable T");
    QueueHandle_t m_handle = nullptr;

public:
    Queue() {
        m_handle = xQueueCreate(N, sizeof(T));
    }

    ~Queue() {
        if (m_handle) vQueueDelete(m_handle);
    }

    // Non-copyable, movable
    Queue(const Queue&) = delete;
    Queue& operator=(const Queue&) = delete;
    Queue(Queue&& other) noexcept : m_handle(other.m_handle) { other.m_handle = nullptr; }

    QueueHandle_t handle() const { return m_handle; }

    // Send — non-blocking (timeout = 0).  Returns false if full.
    bool send(const T& item, TickType_t timeout = 0) {
        return xQueueSend(m_handle, &item, timeout) == pdTRUE;
    }

    // Send — blocking until space available.
    bool send_blocking(const T& item) {
        return xQueueSend(m_handle, &item, portMAX_DELAY) == pdTRUE;
    }

    // Overwrite — always succeeds, drops oldest if full.
    bool overwrite(const T& item) {
        return xQueueOverwrite(m_handle, &item) == pdTRUE;
    }

    // Receive — non-blocking.  Returns false if empty.
    bool receive(T& out, TickType_t timeout = 0) {
        return xQueueReceive(m_handle, &out, timeout) == pdTRUE;
    }

    // Receive — blocking until data available.
    bool receive_blocking(T& out) {
        return xQueueReceive(m_handle, &out, portMAX_DELAY) == pdTRUE;
    }

    // Peek without removing.
    bool peek(T& out, TickType_t timeout = 0) {
        return xQueuePeek(m_handle, &out, timeout) == pdTRUE;
    }

    size_t messages_waiting() const {
        return uxQueueMessagesWaiting(m_handle);
    }

    void reset() {
        xQueueReset(m_handle);
    }
};

} // namespace os
