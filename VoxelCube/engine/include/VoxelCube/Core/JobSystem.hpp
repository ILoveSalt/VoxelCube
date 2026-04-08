#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <type_traits>
#include <utility>
#include <vector>

#include <VoxelCube/Core/Base.hpp>

namespace vc::detail
{
    struct JobState;
}

namespace vc
{
    struct JobSystemSpecification
    {
        std::uint32_t workerCount = 0;
    };

    class JobHandle
    {
    public:
        JobHandle() = default;

        void Wait() const;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] bool IsCompleted() const;
        [[nodiscard]] std::uint64_t GetId() const noexcept;

    private:
        explicit JobHandle(Ref<detail::JobState> state);

    private:
        Ref<detail::JobState> m_state;

        friend class JobSystem;
    };

    class JobSystem
    {
    public:
        static void Initialize(JobSystemSpecification specification = {});
        static void Shutdown();

        [[nodiscard]] static bool IsInitialized();
        [[nodiscard]] static std::uint32_t GetWorkerCount();
        [[nodiscard]] static std::size_t GetQueuedJobCount();
        [[nodiscard]] static std::size_t GetActiveJobCount();
        [[nodiscard]] static bool IsIdle();

        [[nodiscard]] static JobHandle Enqueue(std::function<void()> job);
        static void Wait(const JobHandle& handle);
        static void WaitIdle();

        template <typename Function>
        static void ParallelFor(std::size_t itemCount, Function&& function, std::size_t batchSize = 0)
        {
            static_assert(
                std::is_invocable_v<Function&, std::size_t>,
                "JobSystem::ParallelFor requires a callable accepting a std::size_t index.");

            if (itemCount == 0)
            {
                return;
            }

            if (itemCount == 1)
            {
                std::invoke(function, 0);
                return;
            }

            const std::size_t workerCount = std::max<std::size_t>(1, static_cast<std::size_t>(GetWorkerCount()));
            const std::size_t resolvedBatchSize = batchSize > 0
                ? batchSize
                : std::max<std::size_t>(1, itemCount / (workerCount * 4));

            auto sharedFunction = CreateRef<std::decay_t<Function>>(std::forward<Function>(function));
            std::vector<JobHandle> handles;
            handles.reserve((itemCount + resolvedBatchSize - 1) / resolvedBatchSize);

            for (std::size_t begin = 0; begin < itemCount; begin += resolvedBatchSize)
            {
                const std::size_t end = std::min(begin + resolvedBatchSize, itemCount);
                handles.push_back(Enqueue([sharedFunction, begin, end]()
                {
                    for (std::size_t index = begin; index < end; ++index)
                    {
                        std::invoke(*sharedFunction, index);
                    }
                }));
            }

            for (const JobHandle& handle : handles)
            {
                Wait(handle);
            }
        }
    };
}
