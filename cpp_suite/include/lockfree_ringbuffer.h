#pragma once

#include <cstdint>
#include <cstddef>
#include <atomic>
#include <vector>
#include <algorithm>
#include <cstring>
#include "simd_dsp.h"

// Single-Producer Single-Consumer (SPSC) Lock-Free Circular Ring Buffer
// Engineered with 64-byte cache-line separation to eliminate false sharing.
template <typename T>
class LockFreeSpscRingBuffer {
public:
    explicit LockFreeSpscRingBuffer(size_t capacity_power_of_2 = 8388608) { // Default 8 MB (8,388,608 elements)
        // Ensure capacity is a power of 2 (with overflow protection)
        size_t cap = 1024;
        const size_t max_cap = static_cast<size_t>(1) << (sizeof(size_t) * 8 - 2);
        while (cap < capacity_power_of_2 && cap < max_cap) {
            cap <<= 1;
        }
        capacity_ = cap;
        mask_ = capacity_ - 1;

        // Allocate 64-byte aligned buffer
        buffer_ = static_cast<T*>(SimdDsp::aligned_alloc_64(capacity_ * sizeof(T)));
        if (buffer_) {
            is_custom_aligned_ = true;
        } else {
            // Fallback to standard allocation
            buffer_ = new T[capacity_];
            is_custom_aligned_ = false;
        }
    }

    ~LockFreeSpscRingBuffer() {
        if (buffer_) {
            if (is_custom_aligned_) {
                SimdDsp::aligned_free_64(buffer_);
            } else {
                delete[] buffer_;
            }
            buffer_ = nullptr;
        }
    }

    // Non-copyable
    LockFreeSpscRingBuffer(const LockFreeSpscRingBuffer&) = delete;
    LockFreeSpscRingBuffer& operator=(const LockFreeSpscRingBuffer&) = delete;

    // Push up to 'count' elements into buffer. Returns actual elements written without dropping.
    size_t write(const T* src, size_t count) {
        if (!src || count == 0) return 0;

        const size_t current_write = write_idx_.load(std::memory_order_relaxed);
        const size_t current_read = read_idx_.load(std::memory_order_acquire);

        const size_t free_slots = capacity_ - (current_write - current_read);
        const size_t to_write = std::min(count, free_slots);

        if (to_write == 0) {
            return 0;
        }

        // Two-part circular write
        const size_t write_pos = current_write & mask_;
        const size_t first_chunk = std::min(to_write, capacity_ - write_pos);
        const size_t second_chunk = to_write - first_chunk;

        std::memcpy(&buffer_[write_pos], src, first_chunk * sizeof(T));
        if (second_chunk > 0) {
            std::memcpy(&buffer_[0], src + first_chunk, second_chunk * sizeof(T));
        }

        write_idx_.store(current_write + to_write, std::memory_order_release);
        return to_write;
    }

    // Real-time hardware write: writes what fits, and logs unwritten remainder as dropped
    size_t write_lossy(const T* src, size_t count) {
        size_t written = write(src, count);
        if (written < count) {
            dropped_count_.fetch_add(count - written, std::memory_order_relaxed);
        }
        return written;
    }

    // Read up to 'count' elements into dst. Returns actual elements read.
    size_t read(T* dst, size_t count) {
        if (!dst || count == 0) return 0;

        const size_t current_read = read_idx_.load(std::memory_order_relaxed);
        const size_t current_write = write_idx_.load(std::memory_order_acquire);

        const size_t available = current_write - current_read;
        const size_t to_read = std::min(count, available);

        if (to_read == 0) return 0;

        // Two-part circular read
        const size_t read_pos = current_read & mask_;
        const size_t first_chunk = std::min(to_read, capacity_ - read_pos);
        const size_t second_chunk = to_read - first_chunk;

        std::memcpy(dst, &buffer_[read_pos], first_chunk * sizeof(T));
        if (second_chunk > 0) {
            std::memcpy(dst + first_chunk, &buffer_[0], second_chunk * sizeof(T));
        }

        read_idx_.store(current_read + to_read, std::memory_order_release);
        return to_read;
    }

    // Peek without consuming
    size_t peek(T* dst, size_t count) const {
        if (!dst || count == 0) return 0;

        const size_t current_read = read_idx_.load(std::memory_order_relaxed);
        const size_t current_write = write_idx_.load(std::memory_order_acquire);

        const size_t available = current_write - current_read;
        const size_t to_read = std::min(count, available);

        if (to_read == 0) return 0;

        const size_t read_pos = current_read & mask_;
        const size_t first_chunk = std::min(to_read, capacity_ - read_pos);
        const size_t second_chunk = to_read - first_chunk;

        std::memcpy(dst, &buffer_[read_pos], first_chunk * sizeof(T));
        if (second_chunk > 0) {
            std::memcpy(dst + first_chunk, &buffer_[0], second_chunk * sizeof(T));
        }

        return to_read;
    }

    // Skip / discard count elements
    void discard(size_t count) {
        const size_t current_read = read_idx_.load(std::memory_order_relaxed);
        const size_t current_write = write_idx_.load(std::memory_order_acquire);
        const size_t available = current_write - current_read;
        const size_t to_discard = std::min(count, available);
        read_idx_.store(current_read + to_discard, std::memory_order_release);
    }

    size_t available_read() const {
        const size_t current_write = write_idx_.load(std::memory_order_acquire);
        const size_t current_read = read_idx_.load(std::memory_order_relaxed);
        return current_write - current_read;
    }

    size_t available_write() const {
        const size_t current_write = write_idx_.load(std::memory_order_relaxed);
        const size_t current_read = read_idx_.load(std::memory_order_acquire);
        return capacity_ - (current_write - current_read);
    }

    size_t capacity() const { return capacity_; }

    float fill_percentage() const {
        size_t avail = available_read();
        return (static_cast<float>(avail) / static_cast<float>(capacity_)) * 100.0f;
    }

    uint64_t dropped_count() const {
        return dropped_count_.load(std::memory_order_relaxed);
    }

    void reset() {
        read_idx_.store(0, std::memory_order_relaxed);
        write_idx_.store(0, std::memory_order_relaxed);
        dropped_count_.store(0, std::memory_order_relaxed);
    }

private:
    size_t capacity_ = 0;
    size_t mask_ = 0;
    T* buffer_ = nullptr;
    bool is_custom_aligned_ = false;

    // Cache-line separation (64 bytes) to prevent false sharing between threads
    alignas(64) std::atomic<size_t> write_idx_{0};
    alignas(64) std::atomic<size_t> read_idx_{0};
    alignas(64) std::atomic<uint64_t> dropped_count_{0};
};
