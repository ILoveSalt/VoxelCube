#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <VoxelCube/Core/Base.hpp>
#include <VoxelCube/Core/Timestep.hpp>
#include <VoxelCube/ECS/Registry.hpp>

namespace vc
{
    enum class SystemStage : std::uint8_t
    {
        Startup = 0,
        Update = 1,
        FixedUpdate = 2,
        Shutdown = 3
    };

    [[nodiscard]] inline constexpr std::string_view ToString(SystemStage stage) noexcept
    {
        switch (stage)
        {
            case SystemStage::Startup:
                return "Startup";
            case SystemStage::Update:
                return "Update";
            case SystemStage::FixedUpdate:
                return "FixedUpdate";
            case SystemStage::Shutdown:
                return "Shutdown";
        }

        return "Unknown";
    }

    struct SystemContext
    {
        Registry& registry;
        SystemStage stage = SystemStage::Update;
        Timestep deltaTime {};
        std::uint64_t frameIndex = 0;
        std::uint64_t fixedTickIndex = 0;
    };

    class SystemScheduler
    {
    public:
        using SystemId = std::uint64_t;
        using SystemFunction = std::function<void(SystemContext&)>;

        [[nodiscard]] SystemId RegisterSystem(
            std::string name,
            SystemStage stage,
            SystemFunction function,
            std::int32_t order = 0)
        {
            VC_ASSERT(!name.empty(), "Cannot register an ECS system without a name.");
            VC_ASSERT(static_cast<bool>(function), "Cannot register an ECS system without a callable.");

            const SystemId systemId = m_nextSystemId++;

            SystemEntry entry;
            entry.id = systemId;
            entry.registrationIndex = m_nextRegistrationIndex++;
            entry.name = std::move(name);
            entry.stage = stage;
            entry.order = order;
            entry.function = std::move(function);

            m_systems.push_back(std::move(entry));
            SortSystems();
            return systemId;
        }

        [[nodiscard]] bool UnregisterSystem(SystemId systemId)
        {
            const auto originalSize = m_systems.size();
            m_systems.erase(
                std::remove_if(
                    m_systems.begin(),
                    m_systems.end(),
                    [systemId](const SystemEntry& entry)
                    {
                        return entry.id == systemId;
                    }),
                m_systems.end());
            return m_systems.size() != originalSize;
        }

        [[nodiscard]] bool SetEnabled(SystemId systemId, bool enabled) noexcept
        {
            if (SystemEntry* entry = FindEntry(systemId))
            {
                entry->enabled = enabled;
                return true;
            }

            return false;
        }

        [[nodiscard]] bool IsRegistered(SystemId systemId) const noexcept
        {
            return FindEntry(systemId) != nullptr;
        }

        [[nodiscard]] bool IsEnabled(SystemId systemId) const noexcept
        {
            const SystemEntry* entry = FindEntry(systemId);
            return entry != nullptr && entry->enabled;
        }

        [[nodiscard]] std::size_t GetSystemCount() const noexcept
        {
            return m_systems.size();
        }

        [[nodiscard]] std::size_t GetSystemCount(SystemStage stage) const noexcept
        {
            return static_cast<std::size_t>(std::count_if(
                m_systems.begin(),
                m_systems.end(),
                [stage](const SystemEntry& entry)
                {
                    return entry.stage == stage;
                }));
        }

        [[nodiscard]] std::vector<std::string_view> GetExecutionOrder(SystemStage stage) const
        {
            std::vector<std::string_view> names;
            names.reserve(GetSystemCount(stage));
            for (const SystemEntry& entry : m_systems)
            {
                if (entry.stage == stage)
                {
                    names.push_back(entry.name);
                }
            }

            return names;
        }

        void RunStage(SystemStage stage, SystemContext context)
        {
            context.stage = stage;

            std::vector<SystemId> executionQueue;
            executionQueue.reserve(GetSystemCount(stage));
            for (const SystemEntry& entry : m_systems)
            {
                if (entry.stage == stage && entry.enabled)
                {
                    executionQueue.push_back(entry.id);
                }
            }

            for (const SystemId systemId : executionQueue)
            {
                SystemEntry* entry = FindEntry(systemId);
                if (entry == nullptr || !entry->enabled)
                {
                    continue;
                }

                entry->function(context);
            }
        }

        void Clear() noexcept
        {
            m_systems.clear();
            m_nextSystemId = 1;
            m_nextRegistrationIndex = 0;
        }

    private:
        struct SystemEntry
        {
            SystemId id = 0;
            std::uint64_t registrationIndex = 0;
            std::string name;
            SystemStage stage = SystemStage::Update;
            std::int32_t order = 0;
            bool enabled = true;
            SystemFunction function;
        };

        void SortSystems()
        {
            std::sort(
                m_systems.begin(),
                m_systems.end(),
                [](const SystemEntry& left, const SystemEntry& right)
                {
                    if (left.stage != right.stage)
                    {
                        return static_cast<std::uint8_t>(left.stage) < static_cast<std::uint8_t>(right.stage);
                    }

                    if (left.order != right.order)
                    {
                        return left.order < right.order;
                    }

                    return left.registrationIndex < right.registrationIndex;
                });
        }

        [[nodiscard]] SystemEntry* FindEntry(SystemId systemId) noexcept
        {
            const auto iterator = std::find_if(
                m_systems.begin(),
                m_systems.end(),
                [systemId](const SystemEntry& entry)
                {
                    return entry.id == systemId;
                });
            return iterator != m_systems.end() ? &(*iterator) : nullptr;
        }

        [[nodiscard]] const SystemEntry* FindEntry(SystemId systemId) const noexcept
        {
            const auto iterator = std::find_if(
                m_systems.begin(),
                m_systems.end(),
                [systemId](const SystemEntry& entry)
                {
                    return entry.id == systemId;
                });
            return iterator != m_systems.end() ? &(*iterator) : nullptr;
        }

    private:
        std::vector<SystemEntry> m_systems;
        SystemId m_nextSystemId = 1;
        std::uint64_t m_nextRegistrationIndex = 0;
    };
}
