#pragma once

#include <cstdint>
#include <string>

#include <VoxelCube/Core/Base.hpp>
#include <VoxelCube/Core/Events.hpp>
#include <VoxelCube/Core/Log.hpp>
#include <VoxelCube/Core/Timestep.hpp>

namespace vc
{
    class ConfigDocument;
    class Window;

    struct ApplicationSpecification
    {
        std::string name = "VoxelCube";
        std::uint32_t windowWidth = 1280;
        std::uint32_t windowHeight = 720;
        bool windowResizable = true;
        bool startMaximized = false;
        bool startFullscreen = false;
        LogLevel minimumLogLevel = LogLevel::Trace;
        bool logToConsole = true;
        bool logToFile = true;
        Path logFilePath;
        double fixedTimeStep = 1.0 / 60.0;
        std::uint32_t targetFrameRate = 0;
        double maxDeltaTime = 0.25;
        std::uint64_t maxFrames = 0;
    };

    class Application
    {
    public:
        explicit Application(ApplicationSpecification specification = {});
        virtual ~Application() = default;

        void Run();
        void Close();

        [[nodiscard]] bool IsRunning() const noexcept;
        [[nodiscard]] std::uint64_t GetFrameCount() const noexcept;
        [[nodiscard]] std::uint64_t GetFixedTickCount() const noexcept;
        [[nodiscard]] const ApplicationSpecification& GetSpecification() const noexcept;
        [[nodiscard]] EventBus& GetEventBus() noexcept;
        [[nodiscard]] const EventBus& GetEventBus() const noexcept;
        [[nodiscard]] Window& GetWindow() noexcept;
        [[nodiscard]] const Window& GetWindow() const noexcept;

    protected:
        virtual void OnInitialize();
        virtual void OnShutdown();
        virtual void OnEvent(Event& event);
        virtual void OnUpdate(Timestep deltaTime);
        virtual void OnFixedUpdate(Timestep fixedTimeStep);
        virtual void OnWindowResize(std::uint32_t width, std::uint32_t height);

    private:
        ApplicationSpecification m_specification;
        EventBus m_eventBus;
        Scope<Window> m_window;
        bool m_running = false;
        std::uint64_t m_frameCount = 0;
        std::uint64_t m_fixedTickCount = 0;
    };

    [[nodiscard]] ApplicationSpecification LoadApplicationSpecification(
        const ConfigDocument& document,
        ApplicationSpecification defaults = {});

    [[nodiscard]] ApplicationSpecification LoadApplicationSpecificationFromFile(
        const Path& path,
        ApplicationSpecification defaults = {});
}
