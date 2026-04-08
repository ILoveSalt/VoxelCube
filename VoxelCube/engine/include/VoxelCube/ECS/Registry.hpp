#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <VoxelCube/Core/Base.hpp>

#if defined(VC_HAS_ENTT)
#include <entt/entt.hpp>
#endif

namespace vc
{
    using EntityId = std::uint32_t;
    inline constexpr EntityId NullEntity = std::numeric_limits<EntityId>::max();

    class Registry;

    class Entity
    {
    public:
        Entity() = default;

        [[nodiscard]] EntityId GetId() const noexcept
        {
            return m_id;
        }

        [[nodiscard]] bool IsValid() const noexcept;

        explicit operator bool() const noexcept
        {
            return IsValid();
        }

        void Destroy();

        template <typename Component, typename... Args>
        Component& AddComponent(Args&&... args);

        template <typename Component>
        [[nodiscard]] bool HasComponent() const;

        template <typename Component>
        Component& GetComponent();

        template <typename Component>
        const Component& GetComponent() const;

        template <typename Component>
        void RemoveComponent();

    private:
        Entity(EntityId id, Registry* registry)
            : m_id(id)
            , m_registry(registry)
        {
        }

    private:
        EntityId m_id = NullEntity;
        Registry* m_registry = nullptr;

        friend class Registry;
    };

    class Registry
    {
    public:
        Registry() = default;

        [[nodiscard]] Entity CreateEntity();
        void DestroyEntity(Entity entity);
        void DestroyEntity(EntityId entityId);
        void Clear();

        [[nodiscard]] Entity Wrap(EntityId entityId);
        [[nodiscard]] bool IsValid(Entity entity) const noexcept;
        [[nodiscard]] bool IsValid(EntityId entityId) const noexcept;
        [[nodiscard]] std::size_t GetAliveCount() const noexcept;
        [[nodiscard]] std::string_view GetBackendName() const noexcept;

        template <typename Component, typename... Args>
        Component& AddComponent(Entity entity, Args&&... args)
        {
            VC_ASSERT(IsValid(entity), "Cannot add a component to an invalid entity.");
            VC_ASSERT(!HasComponent<Component>(entity), "Entity already has this component.");

#if defined(VC_HAS_ENTT)
            return m_registry.emplace<Component>(ToNativeEntity(entity.m_id), std::forward<Args>(args)...);
#else
            auto& storage = GetOrCreateStorage<Component>();
            auto [iterator, inserted] = storage.components.emplace(entity.m_id, Component(std::forward<Args>(args)...));
            VC_ASSERT(inserted, "Failed to emplace component into fallback registry storage.");
            return iterator->second;
#endif
        }

        template <typename Component>
        [[nodiscard]] bool HasComponent(Entity entity) const
        {
            if (!IsValid(entity))
            {
                return false;
            }

#if defined(VC_HAS_ENTT)
            return m_registry.all_of<Component>(ToNativeEntity(entity.m_id));
#else
            const auto* storage = TryGetStorage<Component>();
            return storage != nullptr && storage->Contains(entity.m_id);
#endif
        }

        template <typename Component>
        Component& GetComponent(Entity entity)
        {
            VC_ASSERT(HasComponent<Component>(entity), "Entity does not have the requested component.");

#if defined(VC_HAS_ENTT)
            return m_registry.get<Component>(ToNativeEntity(entity.m_id));
#else
            auto& storage = GetOrCreateStorage<Component>();
            return storage.components.at(entity.m_id);
#endif
        }

        template <typename Component>
        const Component& GetComponent(Entity entity) const
        {
            VC_ASSERT(HasComponent<Component>(entity), "Entity does not have the requested component.");

#if defined(VC_HAS_ENTT)
            return m_registry.get<Component>(ToNativeEntity(entity.m_id));
#else
            const auto* storage = TryGetStorage<Component>();
            VC_ASSERT(storage != nullptr, "Component storage is missing in fallback registry.");
            return storage->components.at(entity.m_id);
#endif
        }

        template <typename Component>
        void RemoveComponent(Entity entity)
        {
            if (!HasComponent<Component>(entity))
            {
                return;
            }

#if defined(VC_HAS_ENTT)
            m_registry.remove<Component>(ToNativeEntity(entity.m_id));
#else
            auto* storage = TryGetStorage<Component>();
            VC_ASSERT(storage != nullptr, "Component storage is missing in fallback registry.");
            storage->components.erase(entity.m_id);
#endif
        }

        template <typename Function>
        void Each(Function&& function)
        {
#if defined(VC_HAS_ENTT)
            m_registry.each([this, &function](auto nativeEntity)
            {
                std::invoke(function, Wrap(ToEntityId(nativeEntity)));
            });
#else
            std::vector<EntityId> entities(m_aliveEntities.begin(), m_aliveEntities.end());
            std::sort(entities.begin(), entities.end());
            for (const EntityId entityId : entities)
            {
                std::invoke(function, Wrap(entityId));
            }
#endif
        }

        template <typename... Components, typename Function>
        void View(Function&& function)
        {
            static_assert(sizeof...(Components) > 0, "Registry::View requires at least one component type.");

#if defined(VC_HAS_ENTT)
            auto view = m_registry.view<Components...>();
            for (const auto nativeEntity : view)
            {
                std::invoke(
                    function,
                    Wrap(ToEntityId(nativeEntity)),
                    view.template get<Components>(nativeEntity)...);
            }
#else
            std::vector<EntityId> entities(m_aliveEntities.begin(), m_aliveEntities.end());
            std::sort(entities.begin(), entities.end());
            for (const EntityId entityId : entities)
            {
                const Entity entity = Wrap(entityId);
                if ((HasComponent<Components>(entity) && ...))
                {
                    std::invoke(function, entity, GetComponent<Components>(entity)...);
                }
            }
#endif
        }

    private:
#if defined(VC_HAS_ENTT)
        using NativeEntity = entt::entity;

        [[nodiscard]] static NativeEntity ToNativeEntity(EntityId entityId) noexcept
        {
            return static_cast<NativeEntity>(entityId);
        }

        [[nodiscard]] static EntityId ToEntityId(NativeEntity entity) noexcept
        {
            return static_cast<EntityId>(entity);
        }

        entt::registry m_registry;
#else
        struct IComponentStorage
        {
            virtual ~IComponentStorage() = default;
            virtual void Remove(EntityId entityId) = 0;
            virtual bool Contains(EntityId entityId) const = 0;
            virtual void Clear() = 0;
        };

        template <typename Component>
        struct ComponentStorage final : IComponentStorage
        {
            std::unordered_map<EntityId, Component> components;

            void Remove(EntityId entityId) override
            {
                components.erase(entityId);
            }

            [[nodiscard]] bool Contains(EntityId entityId) const override
            {
                return components.contains(entityId);
            }

            void Clear() override
            {
                components.clear();
            }
        };

        template <typename Component>
        ComponentStorage<Component>& GetOrCreateStorage()
        {
            const std::type_index type = std::type_index(typeid(Component));
            auto iterator = m_componentStorages.find(type);
            if (iterator == m_componentStorages.end())
            {
                iterator = m_componentStorages.emplace(type, std::make_unique<ComponentStorage<Component>>()).first;
            }

            return *static_cast<ComponentStorage<Component>*>(iterator->second.get());
        }

        template <typename Component>
        ComponentStorage<Component>* TryGetStorage()
        {
            const auto iterator = m_componentStorages.find(std::type_index(typeid(Component)));
            return iterator != m_componentStorages.end()
                ? static_cast<ComponentStorage<Component>*>(iterator->second.get())
                : nullptr;
        }

        template <typename Component>
        const ComponentStorage<Component>* TryGetStorage() const
        {
            const auto iterator = m_componentStorages.find(std::type_index(typeid(Component)));
            return iterator != m_componentStorages.end()
                ? static_cast<const ComponentStorage<Component>*>(iterator->second.get())
                : nullptr;
        }

        std::unordered_set<EntityId> m_aliveEntities;
        std::vector<EntityId> m_freeEntities;
        std::unordered_map<std::type_index, std::unique_ptr<IComponentStorage>> m_componentStorages;
        EntityId m_nextEntityId = 1;
#endif
    };

    inline bool Entity::IsValid() const noexcept
    {
        return m_registry != nullptr && m_registry->IsValid(*this);
    }

    inline void Entity::Destroy()
    {
        if (m_registry == nullptr)
        {
            return;
        }

        Registry* registry = m_registry;
        const EntityId entityId = m_id;
        m_registry = nullptr;
        m_id = NullEntity;
        registry->DestroyEntity(entityId);
    }

    template <typename Component, typename... Args>
    Component& Entity::AddComponent(Args&&... args)
    {
        VC_ASSERT(m_registry != nullptr, "Entity is not attached to a registry.");
        return m_registry->AddComponent<Component>(*this, std::forward<Args>(args)...);
    }

    template <typename Component>
    bool Entity::HasComponent() const
    {
        return m_registry != nullptr && m_registry->HasComponent<Component>(*this);
    }

    template <typename Component>
    Component& Entity::GetComponent()
    {
        VC_ASSERT(m_registry != nullptr, "Entity is not attached to a registry.");
        return m_registry->GetComponent<Component>(*this);
    }

    template <typename Component>
    const Component& Entity::GetComponent() const
    {
        VC_ASSERT(m_registry != nullptr, "Entity is not attached to a registry.");
        return m_registry->GetComponent<Component>(*this);
    }

    template <typename Component>
    void Entity::RemoveComponent()
    {
        if (m_registry == nullptr)
        {
            return;
        }

        m_registry->RemoveComponent<Component>(*this);
    }

    inline Entity Registry::CreateEntity()
    {
#if defined(VC_HAS_ENTT)
        return Entity(ToEntityId(m_registry.create()), this);
#else
        EntityId entityId = NullEntity;
        if (!m_freeEntities.empty())
        {
            entityId = m_freeEntities.back();
            m_freeEntities.pop_back();
        }
        else
        {
            entityId = m_nextEntityId++;
        }

        m_aliveEntities.insert(entityId);
        return Entity(entityId, this);
#endif
    }

    inline void Registry::DestroyEntity(Entity entity)
    {
        DestroyEntity(entity.GetId());
    }

    inline void Registry::DestroyEntity(EntityId entityId)
    {
        if (!IsValid(entityId))
        {
            return;
        }

#if defined(VC_HAS_ENTT)
        m_registry.destroy(ToNativeEntity(entityId));
#else
        for (auto& [type, storage] : m_componentStorages)
        {
            (void)type;
            storage->Remove(entityId);
        }

        m_aliveEntities.erase(entityId);
        m_freeEntities.push_back(entityId);
#endif
    }

    inline void Registry::Clear()
    {
#if defined(VC_HAS_ENTT)
        m_registry.clear();
#else
        for (auto& [type, storage] : m_componentStorages)
        {
            (void)type;
            storage->Clear();
        }

        m_aliveEntities.clear();
        m_freeEntities.clear();
        m_componentStorages.clear();
        m_nextEntityId = 1;
#endif
    }

    inline Entity Registry::Wrap(EntityId entityId)
    {
        return IsValid(entityId) ? Entity(entityId, this) : Entity();
    }

    inline bool Registry::IsValid(Entity entity) const noexcept
    {
        return entity.m_registry == this && IsValid(entity.m_id);
    }

    inline bool Registry::IsValid(EntityId entityId) const noexcept
    {
        if (entityId == NullEntity)
        {
            return false;
        }

#if defined(VC_HAS_ENTT)
        return m_registry.valid(ToNativeEntity(entityId));
#else
        return m_aliveEntities.contains(entityId);
#endif
    }

    inline std::size_t Registry::GetAliveCount() const noexcept
    {
#if defined(VC_HAS_ENTT)
        return static_cast<std::size_t>(m_registry.alive());
#else
        return m_aliveEntities.size();
#endif
    }

    inline std::string_view Registry::GetBackendName() const noexcept
    {
#if defined(VC_HAS_ENTT)
        return "EnTT";
#else
        return "BootstrapRegistry";
#endif
    }
}
