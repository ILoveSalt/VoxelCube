#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

#include <VoxelCube/Core/Base.hpp>

namespace vc
{
    struct MemoryStatistics
    {
        std::size_t totalAllocatedBytes = 0;
        std::size_t activeBytes = 0;
        std::size_t peakActiveBytes = 0;
        std::uint64_t allocationCount = 0;
        std::uint64_t deallocationCount = 0;
        std::uint64_t activeAllocationCount = 0;
    };

    class Memory
    {
    public:
        [[nodiscard]] static void* Allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t));
        static void Deallocate(void* pointer) noexcept;

        [[nodiscard]] static MemoryStatistics GetStatistics() noexcept;
        static void ResetStatistics() noexcept;

        template <typename T, typename... Args>
        [[nodiscard]] static T* New(Args&&... args)
        {
            void* storage = Allocate(sizeof(T), alignof(T));
            try
            {
                return new (storage) T(std::forward<Args>(args)...);
            }
            catch (...)
            {
                Deallocate(storage);
                throw;
            }
        }

        template <typename T>
        static void Delete(T* pointer)
        {
            if (pointer == nullptr)
            {
                return;
            }

            std::destroy_at(pointer);
            Deallocate(pointer);
        }
    };

    class ArenaAllocator
    {
    public:
        explicit ArenaAllocator(std::size_t capacity);
        ~ArenaAllocator();

        ArenaAllocator(const ArenaAllocator&) = delete;
        ArenaAllocator& operator=(const ArenaAllocator&) = delete;
        ArenaAllocator(ArenaAllocator&& other) noexcept;
        ArenaAllocator& operator=(ArenaAllocator&& other) noexcept;

        [[nodiscard]] void* Allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t));

        template <typename T, typename... Args>
        [[nodiscard]] T* Create(Args&&... args)
        {
            const std::size_t previousOffset = m_offset;
            void* storage = Allocate(sizeof(T), alignof(T));

            T* object = nullptr;
            try
            {
                object = new (storage) T(std::forward<Args>(args)...);
            }
            catch (...)
            {
                m_offset = previousOffset;
                throw;
            }

            if constexpr (!std::is_trivially_destructible_v<T>)
            {
                try
                {
                    m_destructorRecords.push_back(DestructorRecord {
                        .object = object,
                        .destroy = +[](void* value)
                        {
                            std::destroy_at(static_cast<T*>(value));
                        }
                    });
                }
                catch (...)
                {
                    std::destroy_at(object);
                    m_offset = previousOffset;
                    throw;
                }
            }

            return object;
        }

        void Reset();

        [[nodiscard]] std::size_t GetCapacity() const noexcept;
        [[nodiscard]] std::size_t GetUsed() const noexcept;
        [[nodiscard]] std::size_t GetRemaining() const noexcept;

    private:
        struct DestructorRecord
        {
            void* object = nullptr;
            void (*destroy)(void*) = nullptr;
        };

        void DestroyTrackedObjects() noexcept;
        void ReleaseStorage() noexcept;

    private:
        std::byte* m_storage = nullptr;
        std::size_t m_capacity = 0;
        std::size_t m_offset = 0;
        std::vector<DestructorRecord> m_destructorRecords;
    };

    class PoolAllocator
    {
    public:
        PoolAllocator(
            std::size_t blockSize,
            std::size_t capacity,
            std::size_t alignment = alignof(std::max_align_t));
        ~PoolAllocator();

        PoolAllocator(const PoolAllocator&) = delete;
        PoolAllocator& operator=(const PoolAllocator&) = delete;
        PoolAllocator(PoolAllocator&& other) noexcept;
        PoolAllocator& operator=(PoolAllocator&& other) noexcept;

        [[nodiscard]] void* Allocate();
        void Deallocate(void* pointer) noexcept;

        template <typename T, typename... Args>
        [[nodiscard]] T* Create(Args&&... args)
        {
            static_assert(!std::is_array_v<T>, "PoolAllocator::Create does not support array types.");

            VC_ASSERT(sizeof(T) <= m_blockSize, "PoolAllocator block is too small for the requested object type.");
            VC_ASSERT(alignof(T) <= m_alignment, "PoolAllocator alignment is too small for the requested object type.");

            void* storage = Allocate();
            try
            {
                return new (storage) T(std::forward<Args>(args)...);
            }
            catch (...)
            {
                Deallocate(storage);
                throw;
            }
        }

        template <typename T>
        void Destroy(T* pointer) noexcept
        {
            if (pointer == nullptr)
            {
                return;
            }

            std::destroy_at(pointer);
            Deallocate(pointer);
        }

        [[nodiscard]] std::size_t GetBlockSize() const noexcept;
        [[nodiscard]] std::size_t GetCapacity() const noexcept;
        [[nodiscard]] std::size_t GetFreeCount() const noexcept;
        [[nodiscard]] std::size_t GetUsedCount() const noexcept;

    private:
        void ReleaseStorage() noexcept;
        [[nodiscard]] bool Contains(const void* pointer) const noexcept;
        [[nodiscard]] bool IsBlockAligned(const void* pointer) const noexcept;

    private:
        std::byte* m_storage = nullptr;
        void* m_freeList = nullptr;
        std::size_t m_blockSize = 0;
        std::size_t m_blockStride = 0;
        std::size_t m_capacity = 0;
        std::size_t m_alignment = alignof(std::max_align_t);
        std::size_t m_freeCount = 0;
    };
}
