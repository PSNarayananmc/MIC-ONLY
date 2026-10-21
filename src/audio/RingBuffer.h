#pragma once
#include <atomic>
#include <memory>
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cmath>

namespace vc {

// Lock-free single-producer / single-consumer ring buffer for float audio.
// Producer: PulseAudio capture callback (audio thread).
// Consumer: DSP thread (and vice-versa for the output side).
// All control values are exchanged through std::atomic with acquire/release
// ordering so no mutex is ever taken on the realtime path.
class RingBuffer {
public:
    // capacity is rounded up to a power of two
    explicit RingBuffer(std::size_t capacityPow2 = 1u << 14) { reset(capacityPow2); }

    void reset(std::size_t capacityPow2) {
        std::size_t cap = 1024;
        while (cap < capacityPow2) cap <<= 1;
        m_data.reset(new float[cap]);
        m_capacity = cap;
        m_mask = cap - 1;
        m_read.store(0, std::memory_order_relaxed);
        m_write.store(0, std::memory_order_relaxed);
        m_overruns.store(0, std::memory_order_relaxed);
        m_underruns.store(0, std::memory_order_relaxed);
    }

    std::size_t capacity() const { return m_capacity; }

    // free space available for writing
    std::size_t writeSpace() const {
        const std::size_t w = m_write.load(std::memory_order_relaxed);
        const std::size_t r = m_read.load(std::memory_order_acquire);
        return (r + m_capacity - w - 1) & m_mask;
    }

    // frames available for reading
    std::size_t readSpace() const {
        const std::size_t w = m_write.load(std::memory_order_acquire);
        const std::size_t r = m_read.load(std::memory_order_relaxed);
        return (w - r) & m_mask;
    }

    // Write n frames; returns frames actually written (0 if full).
    std::size_t write(const float* src, std::size_t n) {
        if (writeSpace() < n) {
            m_overruns.fetch_add(1, std::memory_order_relaxed);
            return 0; // drop block: realtime policy, never block
        }
        const std::size_t w = m_write.load(std::memory_order_relaxed);
        const std::size_t first = std::min(n, m_capacity - (w & m_mask));
        std::memcpy(&m_data[w & m_mask], src, first * sizeof(float));
        if (n > first)
            std::memcpy(&m_data[0], src + first, (n - first) * sizeof(float));
        m_write.store((w + n) & m_mask, std::memory_order_release);
        return n;
    }

    // Read n frames; returns frames actually read (0 if empty).
    std::size_t read(float* dst, std::size_t n) {
        if (readSpace() < n) {
            m_underruns.fetch_add(1, std::memory_order_relaxed);
            return 0;
        }
        const std::size_t r = m_read.load(std::memory_order_relaxed);
        const std::size_t first = std::min(n, m_capacity - (r & m_mask));
        std::memcpy(dst, &m_data[r & m_mask], first * sizeof(float));
        if (n > first)
            std::memcpy(dst + first, &m_data[0], (n - first) * sizeof(float));
        m_read.store((r + n) & m_mask, std::memory_order_release);
        return n;
    }

    // Peek without consuming
    std::size_t peek(float* dst, std::size_t n) const {
        if (readSpace() < n) return 0;
        const std::size_t r = m_read.load(std::memory_order_relaxed);
        const std::size_t first = std::min(n, m_capacity - (r & m_mask));
        std::memcpy(dst, &m_data[r & m_mask], first * sizeof(float));
        if (n > first)
            std::memcpy(dst + first, &m_data[0], (n - first) * sizeof(float));
        return n;
    }

    void skip(std::size_t n) {
        if (readSpace() < n) return;
        const std::size_t r = m_read.load(std::memory_order_relaxed);
        m_read.store((r + n) & m_mask, std::memory_order_release);
    }

    std::uint64_t overruns()  const { return m_overruns.load(std::memory_order_relaxed); }
    std::uint64_t underruns() const { return m_underruns.load(std::memory_order_relaxed); }

    // Clear content (used on (re)start to avoid playing stale audio)
    void clear() {
        const std::size_t w = m_write.load(std::memory_order_relaxed);
        m_read.store(w, std::memory_order_release);
    }

private:
    std::unique_ptr<float[]> m_data;
    std::size_t m_capacity = 0;
    std::size_t m_mask = 0;
    std::atomic<std::size_t> m_read{0};
    std::atomic<std::size_t> m_write{0};
    std::atomic<std::uint64_t> m_overruns{0};
    std::atomic<std::uint64_t> m_underruns{0};
};

} // namespace vc
