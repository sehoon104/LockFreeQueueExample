#pragma once
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
// =================== Lock-free SPSC ring (bounded) ===================

template <typename T>
class SPSCQueue {
public:
    explicit SPSCQueue(size_t capacity) {
        if (capacity < 2 || (capacity & (capacity - 1)) != 0) {
            throw std::logic_error("capacity must be a power of 2 and >= 2");
        }
        capacity_ = capacity;
        mask_ = capacity_ - 1;
        buffer_ = std::make_unique<T[]>(capacity_);
        readIndex_.store(0, std::memory_order_relaxed);
        writeIndex_.store(0, std::memory_order_relaxed);
    }

    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;

    // Single producer
    bool try_push(const T& v) {
        const size_t w = writeIndex_.load(std::memory_order_relaxed);
        const size_t next = (w + 1) & mask_;
        if (next == readIndex_.load(std::memory_order_acquire)) {
            return false; // full
        }
        buffer_[w] = v;
        writeIndex_.store(next, std::memory_order_release);
        return true;
    }

    // Single consumer
    bool try_pop(T& out) {
        const size_t r = readIndex_.load(std::memory_order_relaxed);
        if (r == writeIndex_.load(std::memory_order_acquire)) {
            return false; // empty
        }
        out = std::move(buffer_[r]);
        const size_t next = (r + 1) & mask_;
        readIndex_.store(next, std::memory_order_release);
        return true;
    }

private:
    size_t capacity_{};
    size_t mask_{};
    std::unique_ptr<T[]> buffer_;
    alignas(64) std::atomic<size_t> readIndex_{ 0 };
    alignas(64) std::atomic<size_t> writeIndex_{ 0 };
};

// =================== Mutex-based bounded queue with same semantics ===================
template <typename T>
class MutexBoundedQueue {
public:
    explicit MutexBoundedQueue(size_t capacity) {
        if (capacity < 2 || (capacity & (capacity - 1)) != 0) {
            throw std::logic_error("capacity must be a power of 2 and >= 2");
        }
        capacity_ = capacity;
        mask_ = capacity_ - 1;
        buffer_ = std::make_unique<T[]>(capacity_);
        readIndex_ = 0;
        writeIndex_ = 0;
    }

    MutexBoundedQueue(const MutexBoundedQueue&) = delete;
    MutexBoundedQueue& operator=(const MutexBoundedQueue&) = delete;

    // Non-blocking push: returns false if full
    bool try_push(const T& v) {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t next = (writeIndex_ + 1) & mask_;
        if (next == readIndex_) return false;      // full
        buffer_[writeIndex_] = v;
        writeIndex_ = next;
        return true;
    }

    // Non-blocking pop: returns false if empty
    bool try_pop(T& out) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (readIndex_ == writeIndex_) return false; // empty
        out = std::move(buffer_[readIndex_]);
        readIndex_ = (readIndex_ + 1) & mask_;
        return true;
    }

private:
    size_t capacity_{};
    size_t mask_{};
    std::unique_ptr<T[]> buffer_;

    // plain integers — mutex gives all needed ordering
    size_t readIndex_{ 0 };
    size_t writeIndex_{ 0 };

    std::mutex mutex_;
};