#include <stdio.h>
#include <iostream>
#include <atomic>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <new>
#include <thread>
#include <vector>
#include <memory>

template <typename T>
class MPSCQ
{
public:
    MPSCQ(size_t capacity) : capacity_(capacity)
    {
        if (capacity == 0 || (capacity & (capacity - 1))) {
            throw std::logic_error("Capacity must be a power of 2, and greater than 1.");
        }
        mask_ = capacity - 1;
        buffer = std::make_unique<Slot[]>(capacity);

        for (auto i = 0; i < capacity; ++i)
        {
            buffer[i].seq.store(i, std::memory_order_relaxed);
        }

    }

    void operator=(const MPSCQ&)
    {
        throw std::logic_error("Cannot copy!");
    }

    void operator=(MPSCQ&&)
    {
        throw std::logic_error("Cannot move!");
    }

    MPSCQ(const MPSCQ&)
    {
        throw std::logic_error("Cannot copy!");
    }

    MPSCQ(MPSCQ&&)
    {
        throw std::logic_error("Cannot move!");
    }

    ~MPSCQ() = default;

    bool push(const T& item)
    {
        //Reserve slot
        auto pos = writeInd.fetch_add(1, std::memory_order_acq_rel);
        Slot& slot = buffer[pos & mask_];
        auto curSeq = slot.seq.load(std::memory_order_acquire);
        if (curSeq != pos)
            return false;
        slot.data = item;
        slot.seq.store(pos + 1, std::memory_order_release);
        return true;
    }

    bool pop(T& item)
    {
        auto curReadInd = readInd.load(std::memory_order_relaxed);
        Slot& slot = buffer[curReadInd & mask_];
        auto curSeq = slot.seq.load(std::memory_order_acquire);
        if (curSeq != curReadInd + 1)
            return false;//consumer doesn't see it ready

        item = std::move(slot.data);

        slot.seq.store(curReadInd + capacity_, std::memory_order_release);
        readInd.store(curReadInd + 1, std::memory_order_relaxed);

        return true;
    }

private:
    struct Slot {
        T data;
        std::atomic<uint64_t> seq{ 0 };
    };
    std::unique_ptr<Slot[]> buffer;
    size_t mask_;
    size_t capacity_;
    alignas(64) std::atomic<uint64_t> readInd{ 0 };
    alignas(64) std::atomic<uint64_t> writeInd{ 0 };
};

template <typename T>
class SPSCQ
{
public:
    SPSCQ(size_t capacity) :
        capacity_{ capacity },
        items_{ std::make_unique<T[]>(capacity) }
    {
        if (capacity == 0 || (capacity & (capacity - 1))) {
            throw std::logic_error("Capacity must be a power of 2, and greater than 1.");
        }
    }

    void operator=(const SPSCQ&) { throw std::logic_error("Cannot copy!"); };
    SPSCQ(const SPSCQ&) { throw std::logic_error("Cannot copy!"); };

    void operator=(SPSCQ&&) { throw std::logic_error("Cannot move!"); };
    SPSCQ(SPSCQ&&) { throw std::logic_error("Cannot move!"); };

    ~SPSCQ() = default;

    bool push(const T& item)
    {
        const auto writeIdx = writeIdx_.load(std::memory_order_relaxed);
        const auto readIdx = readIdx_.load(std::memory_order_acquire);

        const auto nextWriteIdx = (writeIdx + 1) & (capacity_ - 1);
        const auto isFull = nextWriteIdx == readIdx;
        if (isFull) {
            return false;
        }

        items_[writeIdx] = std::move(item);

        writeIdx_.store(nextWriteIdx, std::memory_order_release);

        return true;
    }

    bool pop(T& item)
    {
        const auto readIdx = readIdx_.load(std::memory_order_relaxed);
        const auto writeIdx = writeIdx_.load(std::memory_order_acquire);
        const auto isEmpty = readIdx == writeIdx;
        if (isEmpty) {
            return false;
        }

        item = std::move(items_[readIdx]);

        const auto nextReadIdx = (readIdx + 1) & (capacity_ - 1);
        readIdx_.store(nextReadIdx, std::memory_order_release);

        return true;
    }

    [[nodiscard]] bool full() const
    {
        const auto writeIdx = writeIdx_.load(std::memory_order_relaxed);
        const auto nextWriteIdx = (writeIdx + 1) & (capacity_ - 1);
        const auto readIdx = readIdx_.load(std::memory_order_relaxed);
        return nextWriteIdx == readIdx;
    }

    [[nodiscard]] size_t size() const
    {
        const auto readIdx = readIdx_.load(std::memory_order_relaxed);
        const auto writeIdx = writeIdx_.load(std::memory_order_relaxed);

        if (writeIdx > readIdx) {
            return writeIdx - readIdx;
        }
        else {
            // This has a branchless version, but it's hard to think of on the spot.
            return capacity_ - (readIdx - writeIdx);
        }
    }

    [[nodiscard]] bool empty() const
    {
        const auto readIdx = readIdx_.load(std::memory_order_relaxed);
        const auto writeIdx = writeIdx_.load(std::memory_order_relaxed);
        return readIdx == writeIdx;
    }

private:

    size_t capacity_;
    std::unique_ptr<T[]> items_;
    alignas(64) std::atomic<size_t> readIdx_{ 0 };
    alignas(64) std::atomic<size_t> writeIdx_{ 0 };
};
