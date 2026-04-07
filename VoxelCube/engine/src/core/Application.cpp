#include <VoxelCube/Core/Application.hpp>

#include <algorithm>
#include <chrono>
#include <string>
#include <thread>

#include <VoxelCube/Core/Events.hpp>
#include <VoxelCube/Core/Input.hpp>
#include <VoxelCube/Core/Log.hpp>
#include <VoxelCube/Core/Window.hpp>

namespace
{
    constexpr double kMinPositiveDelta = 0.0;
}

namespace vc
{
    Application::Application(ApplicationSpecification specification)
        : m_specification(std::move(specification))
    {
        VC_ASSERT(m_specification.fixedTimeStep > 0.0, "Fixed timestep must be greater than zero.");
    }

    void Application::Run()
    {
        VC_ASSERT(!m_running, "Application::Run cannot be called while the application is already running.");

        using Clock = std::chrono::steady_clock;

        m_eventBus.Clear();
        detail::SetActiveEventBus(&m_eventBus);
        m_eventBus.SubscribeAny([this](Event& event) { OnEvent(event); });

        Log::Initialize(LogSpecification {
            .name = m_specification.name,
            .minimumLevel = m_specification.minimumLogLevel,
            .enableConsole = m_specification.logToConsole,
            .enableFile = m_specification.logToFile,
            .filePath = m_specification.logFilePath
        });
        VC_LOG_INFO("Starting application: " + m_specification.name);

        m_window = CreateScope<Window>(WindowSpecification {
            .title = m_specification.name,
            .width = m_specification.windowWidth,
            .height = m_specification.windowHeight,
            .resizable = m_specification.windowResizable,
            .startMaximized = m_specification.startMaximized,
            .startFullscreen = m_specification.startFullscreen
        });
        Input::Initialize(m_window->GetNativeHandle());

        m_running = true;
        m_frameCount = 0;
        m_fixedTickCount = 0;

        OnInitialize();

        auto previousFrameTime = Clock::now();
        double fixedUpdateAccumulator = 0.0;
        const auto targetFrameDuration = m_specification.targetFrameRate > 0
            ? std::chrono::duration<double>(1.0 / static_cast<double>(m_specification.targetFrameRate))
            : std::chrono::duration<double>::zero();

        while (m_running)
        {
            Input::BeginFrame();

            if (!m_window->ProcessMessages())
            {
                VC_LOG_INFO("Window requested shutdown.");
                m_running = false;
                break;
            }

            if (m_window->ConsumeResize())
            {
                OnWindowResize(m_window->GetWidth(), m_window->GetHeight());
            }

            const auto frameStartTime = Clock::now();
            const double rawDeltaTime = std::chrono::duration<double>(frameStartTime - previousFrameTime).count();
            previousFrameTime = frameStartTime;

            const double deltaTimeSeconds = std::clamp(rawDeltaTime, kMinPositiveDelta, m_specification.maxDeltaTime);
            fixedUpdateAccumulator += deltaTimeSeconds;

            while (fixedUpdateAccumulator >= m_specification.fixedTimeStep)
            {
                ++m_fixedTickCount;
                OnFixedUpdate(Timestep(m_specification.fixedTimeStep));
                fixedUpdateAccumulator -= m_specification.fixedTimeStep;
            }

            OnUpdate(Timestep(deltaTimeSeconds));
            ++m_frameCount;

            if (m_specification.maxFrames > 0 && m_frameCount >= m_specification.maxFrames)
            {
                VC_LOG_INFO("Reached configured frame budget, stopping application.");
                Close();
            }

            if (targetFrameDuration > std::chrono::duration<double>::zero())
            {
                const auto elapsed = Clock::now() - frameStartTime;
                if (elapsed < targetFrameDuration)
                {
                    std::this_thread::sleep_for(targetFrameDuration - elapsed);
                }
            }
        }

        OnShutdown();
        m_window.reset();
        Input::Shutdown();
        m_eventBus.Clear();
        detail::SetActiveEventBus(nullptr);
        VC_LOG_INFO("Application shutdown complete.");
        Log::Shutdown();
    }

    void Application::Close()
    {
        detail::EmitActiveEvent<ApplicationCloseRequestedEvent>();

        if (m_window)
        {
            m_window->Close();
        }

        m_running = false;
    }

    bool Application::IsRunning() const noexcept
    {
        return m_running;
    }

    std::uint64_t Application::GetFrameCount() const noexcept
    {
        return m_frameCount;
    }

    std::uint64_t Application::GetFixedTickCount() const noexcept
    {
        return m_fixedTickCount;
    }

    const ApplicationSpecification& Application::GetSpecification() const noexcept
    {
        return m_specification;
    }

    EventBus& Application::GetEventBus() noexcept
    {
        return m_eventBus;
    }

    const EventBus& Application::GetEventBus() const noexcept
    {
        return m_eventBus;
    }

    Window& Application::GetWindow() noexcept
    {
        VC_ASSERT(m_window, "Window is not available.");
        return *m_window;
    }

    const Window& Application::GetWindow() const noexcept
    {
        VC_ASSERT(m_window, "Window is not available.");
        return *m_window;
    }

    void Application::OnInitialize()
    {
    }

    void Application::OnShutdown()
    {
    }

    void Application::OnEvent(Event& event)
    {
        (void)event;
    }

    void Application::OnUpdate(Timestep deltaTime)
    {
        (void)deltaTime;
    }

    void Application::OnFixedUpdate(Timestep fixedTimeStep)
    {
        (void)fixedTimeStep;
    }

    void Application::OnWindowResize(std::uint32_t width, std::uint32_t height)
    {
        (void)width;
        (void)height;
    }
}
