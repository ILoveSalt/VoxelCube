#include <VoxelCube/Core/Memory.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>
#include <utility>

namespace
{
    struct alignas(std::max_align_t) AllocationHeader
    {
        void* rawPointer = nullptr;
        std::size_t size = 0;
    };

    struct MemoryTrackerState
    {
        std::atomic<std::size_t> totalAllocatedBytes = 0;
        std::atomic<std::size_t> activeBytes = 0;
        std::atomic<std::size_t> peakActiveBytes = 0;
        std::atomic<std::uint64_t> allocationCount = 0;
        std::atomic<std::uint64_t> deallocationCount = 0;
        std::atomic<std::uint64_t> activeAllocationCount = 0;
    };

    [[nodiscard]] MemoryTrackerState& GetMemoryTrackerState()
    {
        static MemoryTrackerState state;
        return state;
    }

    [[nodiscard]] constexpr bool IsPowerOfTwo(std::size_t value) noexcept
    {
        return value != 0 && (value & (value - 1)) == 0;
    }

    [[nodiscard]] constexpr std::size_t AlignUp(std::size_t value, std::size_t alignment) noexcept
    {
        return (value + (alignment - 1)) & ~(alignment - 1);
    }

    void UpdatePeakActiveBytes(MemoryTrackerState& state, std::size_t activeBytes) noexcept
    {
        std::size_t currentPeak = state.peakActiveBytes.load(std::memory_order_relaxed);
        while (currentPeak < activeBytes &&
               !state.peakActiveBytes.compare_exchange_weak(
                   currentPeak,
                   activeBytes,
                   std::memory_order_relaxed,
                   std::memory_order_relaxed))
        {
        }
    }
}

namespace vc
{
    void* Memory::Allocate(std::size_t size, std::size_t alignment)
    {
        VC_ASSERT(IsPowerOfTwo(alignment), "Memory::Allocate requires a power-of-two alignment.");

        const std::size_t effectiveAlignment = std::max(alignment, alignof(AllocationHeader));
        const std::size_t allocationSize = std::max<std::size_t>(size, 1);
        const std::size_t rawSize = allocationSize + effectiveAlignment - 1 + sizeof(AllocationHeader);

        auto* rawMemory = static_cast<std::byte*>(::operator new(rawSize));
        const std::uintptr_t candidateAddress = reinterpret_cast<std::uintptr_t>(rawMemory + sizeof(AllocationHeader));
        const std::uintptr_t alignedAddress = AlignUp(candidateAddress, effectiveAlignment);

        auto* header = reinterpret_cast<AllocationHeader*>(alignedAddress - sizeof(AllocationHeader));
        header->rawPointer = rawMemory;
        header->size = size;

        auto& state = GetMemoryTrackerState();
        state.totalAllocatedBytes.fetch_add(size, std::memory_order_relaxed);
        const std::size_t activeBytes = state.activeBytes.fetch_add(size, std::memory_order_relaxed) + size;
        state.allocationCount.fetch_add(1, std::memory_order_relaxed);
        state.activeAllocationCount.fetch_add(1, std::memory_order_relaxed);
        UpdatePeakActiveBytes(state, activeBytes);

        return reinterpret_cast<void*>(alignedAddress);
    }

    void Memory::Deallocate(void* pointer) noexcept
    {
        if (pointer == nullptr)
        {
            return;
        }

        auto* header = reinterpret_cast<AllocationHeader*>(static_cast<std::byte*>(pointer) - sizeof(AllocationHeader));
        auto& state = GetMemoryTrackerState();
        state.activeBytes.fetch_sub(header->size, std::memory_order_relaxed);
        state.deallocationCount.fetch_add(1, std::memory_order_relaxed);
        state.activeAllocationCount.fetch_sub(1, std::memory_order_relaxed);

        ::operator delete(header->rawPointer);
    }

    MemoryStatistics Memory::GetStatistics() noexcept
    {
        const auto& state = GetMemoryTrackerState();
        return MemoryStatistics {
            .totalAllocatedBytes = state.totalAllocatedBytes.load(std::memory_order_relaxed),
            .activeBytes = state.activeBytes.load(std::memory_order_relaxed),
            .peakActiveBytes = state.peakActiveBytes.load(std::memory_order_relaxed),
            .allocationCount = state.allocationCount.load(std::memory_order_relaxed),
            .deallocationCount = state.deallocationCount.load(std::memory_order_relaxed),
            .activeAllocationCount = state.activeAllocationCount.load(std::memory_order_relaxed)
        };
    }

    void Memory::ResetStatistics() noexcept
    {
        auto& state = GetMemoryTrackerState();
        VC_ASSERT(
            state.activeBytes.load(std::memory_order_relaxed) == 0 &&
            state.activeAllocationCount.load(std::memory_order_relaxed) == 0,
            "Cannot reset memory statistics while allocations are still active.");

        state.totalAllocatedBytes.store(0, std::memory_order_relaxed);
        state.activeBytes.store(0, std::memory_order_relaxed);
        state.peakActiveBytes.store(0, std::memory_order_relaxed);
        state.allocationCount.store(0, std::memory_order_relaxed);
        state.deallocationCount.store(0, std::memory_order_relaxed);
        state.activeAllocationCount.store(0, std::memory_order_relaxed);
    }

    ArenaAllocator::ArenaAllocator(std::size_t capacity)
        : m_storage(nullptr)
        , m_capacity(capacity)
    {
        VC_ASSERT(capacity > 0, "ArenaAllocator capacity must be greater than zero.");
        m_storage = static_cast<std::byte*>(Memory::Allocate(capacity));
    }

    ArenaAllocator::~ArenaAllocator()
    {
        ReleaseStorage();
    }

    ArenaAllocator::ArenaAllocator(ArenaAllocator&& other) noexcept
        : m_storage(std::exchange(other.m_storage, nullptr))
        , m_capacity(std::exchange(other.m_capacity, 0))
        , m_offset(std::exchange(other.m_offset, 0))
        , m_destructorRecords(std::move(other.m_destructorRecords))
    {
    }

    ArenaAllocator& ArenaAllocator::operator=(ArenaAllocator&& other) noexcept
    {
        if (this == &other)
        {
            return *this;
        }

        ReleaseStorage();

        m_storage = std::exchange(other.m_storage, nullptr);
        m_capacity = std::exchange(other.m_capacity, 0);
        m_offset = std::exchange(other.m_offset, 0);
        m_destructorRecords = std::move(other.m_destructorRecords);
        return *this;
    }

    void* ArenaAllocator::Allocate(std::size_t size, std::size_t alignment)
    {
        VC_ASSERT(m_storage != nullptr, "ArenaAllocator storage is not initialized.");
        VC_ASSERT(IsPowerOfTwo(alignment), "ArenaAllocator alignment must be a power of two.");

        const std::uintptr_t baseAddress = reinterpret_cast<std::uintptr_t>(m_storage);
        const std::uintptr_t currentAddress = baseAddress + m_offset;
        const std::uintptr_t alignedAddress = AlignUp(currentAddress, alignment);
        const std::size_t newOffset = (alignedAddress - baseAddress) + size;

        if (newOffset > m_capacity)
        {
            throw std::bad_alloc();
        }

        m_offset = newOffset;
        return reinterpret_cast<void*>(alignedAddress);
    }

    void ArenaAllocator::Reset()
    {
        DestroyTrackedObjects();
        m_offset = 0;
    }

    std::size_t ArenaAllocator::GetCapacity() const noexcept
    {
        return m_capacity;
    }

    std::size_t ArenaAllocator::GetUsed() const noexcept
    {
        return m_offset;
    }

    std::size_t ArenaAllocator::GetRemaining() const noexcept
    {
        return m_capacity - m_offset;
    }

    void ArenaAllocator::DestroyTrackedObjects() noexcept
    {
        for (auto iterator = m_destructorRecords.rbegin(); iterator != m_destructorRecords.rend(); ++iterator)
        {
            iterator->destroy(iterator->object);
        }

        m_destructorRecords.clear();
    }

    void ArenaAllocator::ReleaseStorage() noexcept
    {
        if (m_storage == nullptr)
        {
            return;
        }

        DestroyTrackedObjects();
        Memory::Deallocate(m_storage);
        m_storage = nullptr;
        m_capacity = 0;
        m_offset = 0;
    }

    PoolAllocator::PoolAllocator(std::size_t blockSize, std::size_t capacity, std::size_t alignment)
        : m_storage(nullptr)
        , m_freeList(nullptr)
        , m_blockSize(blockSize)
        , m_blockStride(AlignUp(std::max(blockSize, sizeof(void*)), alignment))
        , m_capacity(capacity)
        , m_alignment(alignment)
        , m_freeCount(capacity)
    {
        VC_ASSERT(blockSize > 0, "PoolAllocator block size must be greater than zero.");
        VC_ASSERT(capacity > 0, "PoolAllocator capacity must be greater than zero.");
        VC_ASSERT(IsPowerOfTwo(alignment), "PoolAllocator alignment must be a power of two.");

        m_storage = static_cast<std::byte*>(Memory::Allocate(m_blockStride * m_capacity, m_alignment));

        for (std::size_t index = 0; index < m_capacity; ++index)
        {
            auto* block = m_storage + index * m_blockStride;
            void* next = index + 1 < m_capacity ? static_cast<void*>(block + m_blockStride) : nullptr;
            *reinterpret_cast<void**>(block) = next;
        }

        m_freeList = m_storage;
    }

    PoolAllocator::~PoolAllocator()
    {
        ReleaseStorage();
    }

    PoolAllocator::PoolAllocator(PoolAllocator&& other) noexcept
        : m_storage(std::exchange(other.m_storage, nullptr))
        , m_freeList(std::exchange(other.m_freeList, nullptr))
        , m_blockSize(std::exchange(other.m_blockSize, 0))
        , m_blockStride(std::exchange(other.m_blockStride, 0))
        , m_capacity(std::exchange(other.m_capacity, 0))
        , m_alignment(std::exchange(other.m_alignment, alignof(std::max_align_t)))
        , m_freeCount(std::exchange(other.m_freeCount, 0))
    {
    }

    PoolAllocator& PoolAllocator::operator=(PoolAllocator&& other) noexcept
    {
        if (this == &other)
        {
            return *this;
        }

        ReleaseStorage();

        m_storage = std::exchange(other.m_storage, nullptr);
        m_freeList = std::exchange(other.m_freeList, nullptr);
        m_blockSize = std::exchange(other.m_blockSize, 0);
        m_blockStride = std::exchange(other.m_blockStride, 0);
        m_capacity = std::exchange(other.m_capacity, 0);
        m_alignment = std::exchange(other.m_alignment, alignof(std::max_align_t));
        m_freeCount = std::exchange(other.m_freeCount, 0);
        return *this;
    }

    void* PoolAllocator::Allocate()
    {
        if (m_freeList == nullptr)
        {
            throw std::bad_alloc();
        }

        void* block = m_freeList;
        m_freeList = *reinterpret_cast<void**>(m_freeList);
        --m_freeCount;
        return block;
    }

    void PoolAllocator::Deallocate(void* pointer) noexcept
    {
        if (pointer == nullptr)
        {
            return;
        }

        VC_ASSERT(Contains(pointer), "PoolAllocator received a pointer outside of its storage.");
        VC_ASSERT(IsBlockAligned(pointer), "PoolAllocator received a pointer that is not block-aligned.");

        *reinterpret_cast<void**>(pointer) = m_freeList;
        m_freeList = pointer;
        ++m_freeCount;
        VC_ASSERT(m_freeCount <= m_capacity, "PoolAllocator free count overflow.");
    }

    std::size_t PoolAllocator::GetBlockSize() const noexcept
    {
        return m_blockSize;
    }

    std::size_t PoolAllocator::GetCapacity() const noexcept
    {
        return m_capacity;
    }

    std::size_t PoolAllocator::GetFreeCount() const noexcept
    {
        return m_freeCount;
    }

    std::size_t PoolAllocator::GetUsedCount() const noexcept
    {
        return m_capacity - m_freeCount;
    }

    void PoolAllocator::ReleaseStorage() noexcept
    {
        if (m_storage == nullptr)
        {
            return;
        }

        Memory::Deallocate(m_storage);
        m_storage = nullptr;
        m_freeList = nullptr;
        m_blockSize = 0;
        m_blockStride = 0;
        m_capacity = 0;
        m_freeCount = 0;
    }

    bool PoolAllocator::Contains(const void* pointer) const noexcept
    {
        const auto* bytePointer = static_cast<const std::byte*>(pointer);
        return bytePointer >= m_storage && bytePointer < (m_storage + m_blockStride * m_capacity);
    }

    bool PoolAllocator::IsBlockAligned(const void* pointer) const noexcept
    {
        const auto* bytePointer = static_cast<const std::byte*>(pointer);
        return static_cast<std::size_t>(bytePointer - m_storage) % m_blockStride == 0;
    }
}
