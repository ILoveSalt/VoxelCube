#include <VoxelCube/Core/JobSystem.hpp>

#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <VoxelCube/Core/Log.hpp>

namespace vc::detail
{
    struct JobState
    {
        std::uint64_t id = 0;
        mutable std::mutex mutex;
        std::condition_variable completionCondition;
        bool completed = false;
        std::exception_ptr exception;
    };
}

namespace
{
    struct PendingJob
    {
        vc::Ref<vc::detail::JobState> state;
        std::function<void()> callback;
    };

    struct JobSystemState
    {
        std::mutex mutex;
        std::condition_variable workAvailableCondition;
        std::condition_variable idleCondition;
        std::deque<PendingJob> queuedJobs;
        std::vector<std::thread> workers;
        std::uint64_t nextJobId = 0;
        std::uint32_t workerCount = 0;
        std::size_t activeJobs = 0;
        bool initialized = false;
        bool stopping = false;
    };

    JobSystemState& GetJobSystemState()
    {
        static JobSystemState state;
        return state;
    }

    [[nodiscard]] std::uint32_t ResolveWorkerCount(std::uint32_t requestedCount)
    {
        if (requestedCount > 0)
        {
            return requestedCount;
        }

        const std::uint32_t hardwareThreadCount = std::thread::hardware_concurrency();
        if (hardwareThreadCount <= 1)
        {
            return 1;
        }

        return hardwareThreadCount - 1;
    }

    void FinalizeCompletedJob(JobSystemState& state, PendingJob& job, std::exception_ptr exception)
    {
        {
            std::scoped_lock lock(job.state->mutex);
            job.state->completed = true;
            job.state->exception = std::move(exception);
        }
        job.state->completionCondition.notify_all();

        {
            std::scoped_lock lock(state.mutex);
            VC_ASSERT(state.activeJobs > 0, "JobSystem active job counter underflow.");
            --state.activeJobs;

            if (state.queuedJobs.empty() && state.activeJobs == 0)
            {
                state.idleCondition.notify_all();
            }
        }
    }

    void ExecutePendingJob(JobSystemState& state, PendingJob job)
    {
        std::exception_ptr exception;

        try
        {
            job.callback();
        }
        catch (...)
        {
            exception = std::current_exception();
        }

        FinalizeCompletedJob(state, job, std::move(exception));
    }

    [[nodiscard]] bool TryExecuteOneQueuedJob(JobSystemState& state)
    {
        PendingJob job;

        {
            std::scoped_lock lock(state.mutex);
            if (state.queuedJobs.empty())
            {
                return false;
            }

            job = std::move(state.queuedJobs.front());
            state.queuedJobs.pop_front();
            ++state.activeJobs;
        }

        ExecutePendingJob(state, std::move(job));
        return true;
    }

    void WorkerMain(JobSystemState& state)
    {
        for (;;)
        {
            PendingJob job;

            {
                std::unique_lock lock(state.mutex);
                state.workAvailableCondition.wait(lock, [&state]
                {
                    return state.stopping || !state.queuedJobs.empty();
                });

                if (state.queuedJobs.empty())
                {
                    if (state.stopping)
                    {
                        break;
                    }

                    continue;
                }

                job = std::move(state.queuedJobs.front());
                state.queuedJobs.pop_front();
                ++state.activeJobs;
            }

            ExecutePendingJob(state, std::move(job));
        }
    }
}

namespace vc
{
    JobHandle::JobHandle(Ref<detail::JobState> state)
        : m_state(std::move(state))
    {
    }

    void JobHandle::Wait() const
    {
        JobSystem::Wait(*this);
    }

    bool JobHandle::IsValid() const noexcept
    {
        return static_cast<bool>(m_state);
    }

    bool JobHandle::IsCompleted() const
    {
        if (!m_state)
        {
            return false;
        }

        std::scoped_lock lock(m_state->mutex);
        return m_state->completed;
    }

    std::uint64_t JobHandle::GetId() const noexcept
    {
        return m_state ? m_state->id : 0;
    }

    void JobSystem::Initialize(JobSystemSpecification specification)
    {
        auto& state = GetJobSystemState();

        {
            std::scoped_lock lock(state.mutex);
            VC_ASSERT(!state.initialized, "JobSystem is already initialized.");

            state.stopping = false;
            state.initialized = true;
            state.workerCount = ResolveWorkerCount(specification.workerCount);
            state.nextJobId = 0;
            state.activeJobs = 0;
            state.queuedJobs.clear();
            state.workers.clear();
            state.workers.reserve(state.workerCount);

            for (std::uint32_t index = 0; index < state.workerCount; ++index)
            {
                state.workers.emplace_back([&state]()
                {
                    WorkerMain(state);
                });
            }
        }

        VC_LOG_INFO("JobSystem initialized with " + std::to_string(state.workerCount) + " worker threads.");
    }

    void JobSystem::Shutdown()
    {
        auto& state = GetJobSystemState();
        std::vector<std::thread> workers;

        {
            std::scoped_lock lock(state.mutex);
            if (!state.initialized)
            {
                return;
            }

            state.stopping = true;
            workers = std::move(state.workers);
        }

        state.workAvailableCondition.notify_all();

        for (std::thread& worker : workers)
        {
            if (worker.joinable())
            {
                worker.join();
            }
        }

        {
            std::scoped_lock lock(state.mutex);
            VC_ASSERT(state.queuedJobs.empty(), "JobSystem queue was not fully drained during shutdown.");
            VC_ASSERT(state.activeJobs == 0, "JobSystem still had active jobs during shutdown.");

            state.workerCount = 0;
            state.nextJobId = 0;
            state.initialized = false;
            state.stopping = false;
            state.workers.clear();
        }

        VC_LOG_INFO("JobSystem shutdown complete.");
    }

    bool JobSystem::IsInitialized()
    {
        auto& state = GetJobSystemState();
        std::scoped_lock lock(state.mutex);
        return state.initialized;
    }

    std::uint32_t JobSystem::GetWorkerCount()
    {
        auto& state = GetJobSystemState();
        std::scoped_lock lock(state.mutex);
        return state.workerCount;
    }

    std::size_t JobSystem::GetQueuedJobCount()
    {
        auto& state = GetJobSystemState();
        std::scoped_lock lock(state.mutex);
        return state.queuedJobs.size();
    }

    std::size_t JobSystem::GetActiveJobCount()
    {
        auto& state = GetJobSystemState();
        std::scoped_lock lock(state.mutex);
        return state.activeJobs;
    }

    bool JobSystem::IsIdle()
    {
        auto& state = GetJobSystemState();
        std::scoped_lock lock(state.mutex);
        return state.queuedJobs.empty() && state.activeJobs == 0;
    }

    JobHandle JobSystem::Enqueue(std::function<void()> job)
    {
        VC_ASSERT(static_cast<bool>(job), "JobSystem::Enqueue requires a valid callback.");

        auto& state = GetJobSystemState();
        Ref<detail::JobState> jobState = CreateRef<detail::JobState>();

        {
            std::scoped_lock lock(state.mutex);
            VC_ASSERT(state.initialized, "JobSystem must be initialized before queuing jobs.");
            VC_ASSERT(!state.stopping, "JobSystem is shutting down and no longer accepts new jobs.");

            jobState->id = ++state.nextJobId;
            state.queuedJobs.push_back(PendingJob {
                .state = jobState,
                .callback = std::move(job)
            });
        }

        state.workAvailableCondition.notify_one();
        return JobHandle(std::move(jobState));
    }

    void JobSystem::Wait(const JobHandle& handle)
    {
        if (!handle.m_state)
        {
            return;
        }

        auto& state = GetJobSystemState();

        for (;;)
        {
            {
                std::scoped_lock lock(handle.m_state->mutex);
                if (handle.m_state->completed)
                {
                    break;
                }
            }

            if (!TryExecuteOneQueuedJob(state))
            {
                std::unique_lock lock(handle.m_state->mutex);
                handle.m_state->completionCondition.wait(lock, [&handle]
                {
                    return handle.m_state->completed;
                });
                break;
            }
        }

        std::exception_ptr exception;
        {
            std::scoped_lock lock(handle.m_state->mutex);
            exception = handle.m_state->exception;
        }

        if (exception)
        {
            std::rethrow_exception(exception);
        }
    }

    void JobSystem::WaitIdle()
    {
        auto& state = GetJobSystemState();

        for (;;)
        {
            {
                std::scoped_lock lock(state.mutex);
                if (!state.initialized || (state.queuedJobs.empty() && state.activeJobs == 0))
                {
                    return;
                }
            }

            if (!TryExecuteOneQueuedJob(state))
            {
                std::unique_lock lock(state.mutex);
                state.idleCondition.wait(lock, [&state]
                {
                    return state.queuedJobs.empty() && state.activeJobs == 0;
                });
                return;
            }
        }
    }
}
