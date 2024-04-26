#pragma once
#include "logger.h"
#include <coreinit/memexpheap.h>
#include <coreinit/memheap.h>
#include <cstdint>
#include <cstring>
#include <optional>

typedef void (*FreeMemoryFn)(void *);

class MemoryWrapper {
public:
    MemoryWrapper(void *ptr, uint32_t size, FreeMemoryFn freeFn) : mPtr(ptr), mSize(size), mFreeFn(freeFn) {
    }
    ~MemoryWrapper() {
        if (mPtr && mFreeFn) {
            memset(mPtr, 0, mSize);
            mFreeFn(mPtr);
        }
    }

    MemoryWrapper(const MemoryWrapper &) = delete;
    MemoryWrapper &operator=(const MemoryWrapper &) = delete;

    MemoryWrapper(MemoryWrapper &&other) noexcept
        : mPtr(other.mPtr), mSize(other.mSize), mFreeFn(other.mFreeFn) {
        other.mPtr    = {};
        other.mSize   = {};
        other.mFreeFn = {};
    }

    MemoryWrapper &operator=(MemoryWrapper &&other) noexcept {
        if (this != &other) {
            mPtr          = other.mPtr;
            mSize         = other.mSize;
            mFreeFn       = other.mFreeFn;
            other.mPtr    = {};
            other.mSize   = 0;
            other.mFreeFn = {};
        }
        return *this;
    }

    [[nodiscard]] void *data() const {
        return mPtr;
    }

    [[nodiscard]] uint32_t size() const {
        return mSize;
    }

    [[nodiscard]] bool IsAllocated() const {
        return mFreeFn && mPtr && mSize > 0;
    }

private:
    void *mPtr           = {};
    uint32_t mSize       = 0;
    FreeMemoryFn mFreeFn = {};
};


class ExpHeapMemory {

public:
    ExpHeapMemory(MEMHeapHandle heapHandle, void *data, uint32_t size) : mHeapHandle(heapHandle),
                                                                         mData(data),
                                                                         mSize(size) {
    }
    ExpHeapMemory() = default;

    ~ExpHeapMemory() {
        if (mData) {
            MEMFreeToExpHeap(mHeapHandle, mData);
        }
        mData = nullptr;
        mSize = 0;
    }

    // Delete the copy constructor and copy assignment operator
    ExpHeapMemory(const ExpHeapMemory &) = delete;
    ExpHeapMemory &operator=(const ExpHeapMemory &) = delete;

    ExpHeapMemory(ExpHeapMemory &&other) noexcept
        : mHeapHandle(other.mHeapHandle), mData(other.mData), mSize(other.mSize) {
        other.mHeapHandle = {};
        other.mData       = {};
        other.mSize       = 0;
    }

    ExpHeapMemory &operator=(ExpHeapMemory &&other) noexcept {
        if (this != &other) {
            mHeapHandle       = other.mHeapHandle;
            mData             = other.mData;
            mSize             = other.mSize;
            other.mHeapHandle = {};
            other.mData       = {};
            other.mSize       = 0;
        }
        return *this;
    }

    explicit operator bool() const {
        return mData != nullptr;
    }

    explicit operator void *() const {
        // Return the desired void* value
        return mData;
    }

    [[nodiscard]] void *data() const {
        return mData;
    }

    [[nodiscard]] std::size_t size() const {
        return mSize;
    }

    static std::optional<ExpHeapMemory> Alloc(MEMHeapHandle heapHandle, uint32_t size, int32_t alignment) {
        auto *ptr = MEMAllocFromExpHeapEx(heapHandle, size, alignment);
        if (!ptr) {
            return {};
        }

        return ExpHeapMemory(heapHandle, ptr, size);
    }

private:
    MEMHeapHandle mHeapHandle{};
    void *mData = nullptr;
    uint32_t mSize{};
};

class HeapWrapper {
public:
    explicit HeapWrapper(MemoryWrapper &&memory) : mMemory(std::move(memory)) {
        mHeapHandle = MEMCreateExpHeapEx(mMemory.data(), mMemory.size(), MEM_HEAP_FLAG_USE_LOCK);
        if (mHeapHandle) {
            mSize = mMemory.size();
            mPtr  = mMemory.data();
        }
    }

    ~HeapWrapper() {
        if (mHeapHandle) {
            MEMDestroyExpHeap(mHeapHandle);
        }
        if (mPtr) {
            memset(mPtr, 0, mSize);
        }
    }

    // Delete the copy constructor and copy assignment operator
    HeapWrapper(const HeapWrapper &) = delete;
    HeapWrapper &operator=(const HeapWrapper &) = delete;

    HeapWrapper(HeapWrapper &&other) noexcept
        : mMemory(std::move(other.mMemory)), mHeapHandle(other.mHeapHandle), mPtr(other.mPtr), mSize(other.mSize) {
        other.mHeapHandle = {};
        other.mPtr        = {};
        other.mSize       = 0;
    }

    HeapWrapper &operator=(HeapWrapper &&other) noexcept {
        if (this != &other) {
            mMemory           = std::move(other.mMemory);
            mHeapHandle       = other.mHeapHandle;
            mPtr              = other.mPtr;
            mSize             = other.mSize;
            other.mHeapHandle = {};
            other.mPtr        = {};
            other.mSize       = 0;
        }
        return *this;
    }

    [[nodiscard]] MEMHeapHandle GetHeapHandle() const {
        return mHeapHandle;
    }

    [[nodiscard]] uint32_t GetHeapSize() const {
        return mSize;
    }

    [[nodiscard]] bool IsAllocated() const {
        return mMemory.IsAllocated();
    }

    [[nodiscard]] std::optional<ExpHeapMemory> Alloc(uint32_t size, int align) const {
        return ExpHeapMemory::Alloc(mHeapHandle, size, align);
    }

private:
    MemoryWrapper mMemory;
    MEMHeapHandle mHeapHandle = {};
    void *mPtr                = {};
    uint32_t mSize            = 0;
};
