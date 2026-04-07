#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

#include <VoxelCube/Core/Input.hpp>

namespace vc
{
    enum class EventCategory : std::uint32_t
    {
        None = 0,
        Application = 1u << 0,
        Window = 1u << 1,
        Input = 1u << 2,
        Keyboard = 1u << 3,
        Mouse = 1u << 4
    };

    [[nodiscard]] constexpr std::uint32_t ToUnderlying(EventCategory category) noexcept
    {
        return static_cast<std::uint32_t>(category);
    }

    [[nodiscard]] constexpr EventCategory operator|(EventCategory left, EventCategory right) noexcept
    {
        return static_cast<EventCategory>(ToUnderlying(left) | ToUnderlying(right));
    }

    class Event
    {
    public:
        virtual ~Event() = default;

        [[nodiscard]] virtual std::string_view GetName() const noexcept = 0;
        [[nodiscard]] virtual std::uint32_t GetCategoryFlags() const noexcept = 0;

        void StopPropagation() noexcept
        {
            m_handled = true;
        }

        [[nodiscard]] bool IsHandled() const noexcept
        {
            return m_handled;
        }

        [[nodiscard]] bool IsInCategory(EventCategory category) const noexcept
        {
            return (GetCategoryFlags() & ToUnderlying(category)) != 0;
        }

    private:
        bool m_handled = false;
    };

    class EventBus
    {
    public:
        using SubscriptionId = std::uint64_t;

        EventBus() = default;
        EventBus(const EventBus&) = delete;
        EventBus& operator=(const EventBus&) = delete;
        EventBus(EventBus&&) = delete;
        EventBus& operator=(EventBus&&) = delete;

        template <typename EventT, typename Handler>
        SubscriptionId Subscribe(Handler&& handler)
        {
            static_assert(std::is_base_of_v<Event, EventT>, "EventBus::Subscribe requires an event type.");

            auto& subscribers = m_subscribers[std::type_index(typeid(EventT))];
            const SubscriptionId id = ++m_nextSubscriptionId;

            subscribers.push_back(Subscriber {
                .id = id,
                .active = true,
                .callback = [capturedHandler = std::forward<Handler>(handler)](Event& event) mutable
                {
                    capturedHandler(static_cast<EventT&>(event));
                }
            });

            return id;
        }

        template <typename Handler>
        SubscriptionId SubscribeAny(Handler&& handler)
        {
            const SubscriptionId id = ++m_nextSubscriptionId;
            m_anySubscribers.push_back(Subscriber {
                .id = id,
                .active = true,
                .callback = std::forward<Handler>(handler)
            });
            return id;
        }

        bool Unsubscribe(SubscriptionId id)
        {
            bool removed = MarkInactive(m_anySubscribers, id);
            for (auto& [eventType, subscribers] : m_subscribers)
            {
                (void)eventType;
                removed = MarkInactive(subscribers, id) || removed;
            }

            if (m_dispatchDepth == 0)
            {
                CompactAllSubscribers();
            }

            return removed;
        }

        template <typename EventT>
        void Emit(EventT& event)
        {
            static_assert(std::is_base_of_v<Event, EventT>, "EventBus::Emit requires an event type.");

            DispatchSubscribers(m_anySubscribers, event);
            if (!event.IsHandled())
            {
                const auto subscribersIterator = m_subscribers.find(std::type_index(typeid(EventT)));
                if (subscribersIterator != m_subscribers.end())
                {
                    DispatchSubscribers(subscribersIterator->second, event);
                }
            }

            if (m_dispatchDepth == 0)
            {
                CompactAllSubscribers();
            }
        }

        void Clear()
        {
            m_subscribers.clear();
            m_anySubscribers.clear();
            m_nextSubscriptionId = 0;
            m_dispatchDepth = 0;
        }

    private:
        struct Subscriber
        {
            SubscriptionId id = 0;
            bool active = true;
            std::function<void(Event&)> callback;
        };

        static bool MarkInactive(std::vector<Subscriber>& subscribers, SubscriptionId id)
        {
            for (Subscriber& subscriber : subscribers)
            {
                if (subscriber.id == id && subscriber.active)
                {
                    subscriber.active = false;
                    return true;
                }
            }

            return false;
        }

        template <typename EventT>
        void DispatchSubscribers(std::vector<Subscriber>& subscribers, EventT& event)
        {
            ++m_dispatchDepth;
            for (Subscriber& subscriber : subscribers)
            {
                if (!subscriber.active)
                {
                    continue;
                }

                subscriber.callback(event);
                if (event.IsHandled())
                {
                    break;
                }
            }
            --m_dispatchDepth;
        }

        static void CompactSubscribers(std::vector<Subscriber>& subscribers)
        {
            subscribers.erase(
                std::remove_if(
                    subscribers.begin(),
                    subscribers.end(),
                    [](const Subscriber& subscriber) { return !subscriber.active; }),
                subscribers.end());
        }

        void CompactAllSubscribers()
        {
            CompactSubscribers(m_anySubscribers);

            for (auto iterator = m_subscribers.begin(); iterator != m_subscribers.end();)
            {
                CompactSubscribers(iterator->second);
                if (iterator->second.empty())
                {
                    iterator = m_subscribers.erase(iterator);
                }
                else
                {
                    ++iterator;
                }
            }
        }

    private:
        std::unordered_map<std::type_index, std::vector<Subscriber>> m_subscribers;
        std::vector<Subscriber> m_anySubscribers;
        SubscriptionId m_nextSubscriptionId = 0;
        std::uint32_t m_dispatchDepth = 0;
    };

    struct ApplicationCloseRequestedEvent final : Event
    {
        [[nodiscard]] std::string_view GetName() const noexcept override
        {
            return "ApplicationCloseRequested";
        }

        [[nodiscard]] std::uint32_t GetCategoryFlags() const noexcept override
        {
            return ToUnderlying(EventCategory::Application);
        }
    };

    struct WindowCloseEvent final : Event
    {
        [[nodiscard]] std::string_view GetName() const noexcept override
        {
            return "WindowClose";
        }

        [[nodiscard]] std::uint32_t GetCategoryFlags() const noexcept override
        {
            return ToUnderlying(EventCategory::Window);
        }
    };

    struct WindowResizeEvent final : Event
    {
        WindowResizeEvent(std::uint32_t eventWidth, std::uint32_t eventHeight)
            : width(eventWidth)
            , height(eventHeight)
        {
        }

        std::uint32_t width = 0;
        std::uint32_t height = 0;

        [[nodiscard]] std::string_view GetName() const noexcept override
        {
            return "WindowResize";
        }

        [[nodiscard]] std::uint32_t GetCategoryFlags() const noexcept override
        {
            return ToUnderlying(EventCategory::Window);
        }
    };

    struct WindowFullscreenChangedEvent final : Event
    {
        explicit WindowFullscreenChangedEvent(bool eventFullscreen)
            : fullscreen(eventFullscreen)
        {
        }

        bool fullscreen = false;

        [[nodiscard]] std::string_view GetName() const noexcept override
        {
            return "WindowFullscreenChanged";
        }

        [[nodiscard]] std::uint32_t GetCategoryFlags() const noexcept override
        {
            return ToUnderlying(EventCategory::Window);
        }
    };

    struct KeyPressedEvent final : Event
    {
        KeyPressedEvent(KeyCode eventKey, bool eventRepeated)
            : key(eventKey)
            , repeated(eventRepeated)
        {
        }

        KeyCode key = KeyCode::Unknown;
        bool repeated = false;

        [[nodiscard]] std::string_view GetName() const noexcept override
        {
            return "KeyPressed";
        }

        [[nodiscard]] std::uint32_t GetCategoryFlags() const noexcept override
        {
            return ToUnderlying(EventCategory::Input | EventCategory::Keyboard);
        }
    };

    struct KeyReleasedEvent final : Event
    {
        explicit KeyReleasedEvent(KeyCode eventKey)
            : key(eventKey)
        {
        }

        KeyCode key = KeyCode::Unknown;

        [[nodiscard]] std::string_view GetName() const noexcept override
        {
            return "KeyReleased";
        }

        [[nodiscard]] std::uint32_t GetCategoryFlags() const noexcept override
        {
            return ToUnderlying(EventCategory::Input | EventCategory::Keyboard);
        }
    };

    struct MouseMovedEvent final : Event
    {
        MouseMovedEvent(std::int32_t eventX, std::int32_t eventY)
            : x(eventX)
            , y(eventY)
        {
        }

        std::int32_t x = 0;
        std::int32_t y = 0;

        [[nodiscard]] std::string_view GetName() const noexcept override
        {
            return "MouseMoved";
        }

        [[nodiscard]] std::uint32_t GetCategoryFlags() const noexcept override
        {
            return ToUnderlying(EventCategory::Input | EventCategory::Mouse);
        }
    };

    struct MouseScrolledEvent final : Event
    {
        explicit MouseScrolledEvent(float eventDelta)
            : delta(eventDelta)
        {
        }

        float delta = 0.0f;

        [[nodiscard]] std::string_view GetName() const noexcept override
        {
            return "MouseScrolled";
        }

        [[nodiscard]] std::uint32_t GetCategoryFlags() const noexcept override
        {
            return ToUnderlying(EventCategory::Input | EventCategory::Mouse);
        }
    };

    struct MouseButtonPressedEvent final : Event
    {
        explicit MouseButtonPressedEvent(MouseButton eventButton)
            : button(eventButton)
        {
        }

        MouseButton button = MouseButton::Left;

        [[nodiscard]] std::string_view GetName() const noexcept override
        {
            return "MouseButtonPressed";
        }

        [[nodiscard]] std::uint32_t GetCategoryFlags() const noexcept override
        {
            return ToUnderlying(EventCategory::Input | EventCategory::Mouse);
        }
    };

    struct MouseButtonReleasedEvent final : Event
    {
        explicit MouseButtonReleasedEvent(MouseButton eventButton)
            : button(eventButton)
        {
        }

        MouseButton button = MouseButton::Left;

        [[nodiscard]] std::string_view GetName() const noexcept override
        {
            return "MouseButtonReleased";
        }

        [[nodiscard]] std::uint32_t GetCategoryFlags() const noexcept override
        {
            return ToUnderlying(EventCategory::Input | EventCategory::Mouse);
        }
    };

    struct MouseRawDeltaEvent final : Event
    {
        MouseRawDeltaEvent(std::int32_t eventDeltaX, std::int32_t eventDeltaY)
            : deltaX(eventDeltaX)
            , deltaY(eventDeltaY)
        {
        }

        std::int32_t deltaX = 0;
        std::int32_t deltaY = 0;

        [[nodiscard]] std::string_view GetName() const noexcept override
        {
            return "MouseRawDelta";
        }

        [[nodiscard]] std::uint32_t GetCategoryFlags() const noexcept override
        {
            return ToUnderlying(EventCategory::Input | EventCategory::Mouse);
        }
    };

    namespace detail
    {
        inline EventBus* g_activeEventBus = nullptr;

        [[nodiscard]] inline EventBus* GetActiveEventBus() noexcept
        {
            return g_activeEventBus;
        }

        inline void SetActiveEventBus(EventBus* eventBus) noexcept
        {
            g_activeEventBus = eventBus;
        }

        template <typename EventT, typename... Args>
        void EmitActiveEvent(Args&&... args)
        {
            static_assert(std::is_base_of_v<Event, EventT>, "EmitActiveEvent requires an event type.");

            EventBus* eventBus = GetActiveEventBus();
            if (eventBus == nullptr)
            {
                return;
            }

            EventT event(std::forward<Args>(args)...);
            eventBus->Emit(event);
        }
    }
}
